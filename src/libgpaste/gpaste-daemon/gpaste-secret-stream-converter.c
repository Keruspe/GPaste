// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-secret-stream-converter.h>

#define GCR_API_SUBJECT_TO_CHANGE
#include <gcr/gcr.h>

#include <sodium.h>

/* A bidirectional GConverter built on libsodium's secretstream (XChaCha20-
 * Poly1305). The symmetric key is derived from a user passphrase with
 * crypto_pwhash (Argon2id); the random salt and the Argon2 parameters are
 * stored in the stream header so decryption can reproduce the key.
 *
 * Stream layout:
 *
 *   MAGIC (8)  salt (16)  opslimit (u64 LE)  memlimit (u64 LE)  ss_header (24)
 *   then, repeated until the FINAL chunk:  clen (u32 LE)  ciphertext (clen)
 *
 * Each plaintext chunk is at most CHUNK_SIZE bytes; the last one carries the
 * secretstream FINAL tag so truncation is detected. Both directions buffer
 * internally (a chunk worth of input, the produced output) so the converter
 * copes with the arbitrary split of buffers GConverter hands it. */

#define G_PASTE_SECRET_STREAM_MAGIC     "GPSTENC1"
#define G_PASTE_SECRET_STREAM_MAGIC_LEN 8

#define CHUNK_SIZE      4096
#define KEYBYTES        crypto_secretstream_xchacha20poly1305_KEYBYTES
#define HEADERBYTES     crypto_secretstream_xchacha20poly1305_HEADERBYTES
#define ABYTES          crypto_secretstream_xchacha20poly1305_ABYTES
#define TAG_MESSAGE     crypto_secretstream_xchacha20poly1305_TAG_MESSAGE
#define TAG_FINAL       crypto_secretstream_xchacha20poly1305_TAG_FINAL
#define SALTBYTES       crypto_pwhash_SALTBYTES

/* Argon2 cost parameters used when deriving the key for a *new* stream; the
 * actual values are stored in each stream's header, so decryption always reads
 * them back from there rather than relying on these. */
#define OPSLIMIT        crypto_pwhash_OPSLIMIT_MODERATE
#define MEMLIMIT        crypto_pwhash_MEMLIMIT_MODERATE

#define STREAM_HEADER_LEN (G_PASTE_SECRET_STREAM_MAGIC_LEN + SALTBYTES + 8 + 8 + HEADERBYTES)

struct _GPasteSecretStreamConverter
{
    GObject parent_instance;
};

typedef struct
{
    GPasteSecretStreamDirection direction;
    /* Kept (in gcr secure memory) for the converter's lifetime: the key is
     * derived lazily and salt-dependent, so reset() must be able to re-derive
     * it for a fresh stream (whose salt may differ). */
    guchar                     *passphrase;
    gsize                       passphrase_len;

    gboolean                    header_done; /* header emitted (encrypt) or parsed (decrypt) */
    gboolean                    finished;    /* FINAL chunk produced/consumed */

    crypto_secretstream_xchacha20poly1305_state state;
    guchar                     *key; /* KEYBYTES, in gcr secure (non-swappable) memory */

    GByteArray                 *in;  /* input not processed yet */
    GByteArray                 *out; /* output not handed back yet */
} GPasteSecretStreamConverterPrivate;

static void g_paste_secret_stream_converter_iface_init (GConverterIface *iface);

G_PASTE_DEFINE_TYPE_WITH_PRIVATE_AND_INTERFACE (SecretStreamConverter, secret_stream_converter, G_TYPE_OBJECT,
                                                G_TYPE_CONVERTER, g_paste_secret_stream_converter_iface_init)

static void
append_u32_le (GByteArray *array,
               guint32     value)
{
    guint32 le = GUINT32_TO_LE (value);

    g_byte_array_append (array, (const guint8 *) &le, sizeof (le));
}

static void
append_u64_le (GByteArray *array,
               guint64     value)
{
    guint64 le = GUINT64_TO_LE (value);

    g_byte_array_append (array, (const guint8 *) &le, sizeof (le));
}

static guint32
read_u32_le (const guint8 *data)
{
    guint32 le;

    memcpy (&le, data, sizeof (le));

    return GUINT32_FROM_LE (le);
}

static guint64
read_u64_le (const guint8 *data)
{
    guint64 le;

    memcpy (&le, data, sizeof (le));

    return GUINT64_FROM_LE (le);
}

/* Encrypt the first @len buffered bytes into a length-prefixed frame. */
static void
push_chunk (GPasteSecretStreamConverterPrivate *priv,
            gsize                               len,
            unsigned char                       tag)
{
    unsigned char cipher[CHUNK_SIZE + ABYTES];
    unsigned long long clen = 0;

    crypto_secretstream_xchacha20poly1305_push (&priv->state, cipher, &clen,
                                                priv->in->data, len, NULL, 0, tag);

    append_u32_le (priv->out, (guint32) clen);
    g_byte_array_append (priv->out, cipher, clen);

    if (len)
        g_byte_array_remove_range (priv->in, 0, len);
}

static gboolean
derive_key (GPasteSecretStreamConverterPrivate *priv,
            const unsigned char                *salt,
            guint64                             opslimit,
            guint64                             memlimit,
            GError                            **error)
{
    if (crypto_pwhash (priv->key, KEYBYTES,
                       (const char *) priv->passphrase, priv->passphrase_len,
                       salt, opslimit, (size_t) memlimit,
                       crypto_pwhash_ALG_ARGON2ID13) != 0)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Could not derive the encryption key (out of memory?)");
        return FALSE;
    }

    return TRUE;
}

static gboolean
encrypt_process (GPasteSecretStreamConverterPrivate *priv,
                 gboolean                            at_end,
                 GError                            **error)
{
    if (!priv->header_done)
    {
        unsigned char salt[SALTBYTES];
        unsigned char header[HEADERBYTES];
        guint64 opslimit = OPSLIMIT;
        guint64 memlimit = MEMLIMIT;

        randombytes_buf (salt, sizeof (salt));

        if (!derive_key (priv, salt, opslimit, memlimit, error))
            return FALSE;

        crypto_secretstream_xchacha20poly1305_init_push (&priv->state, header, priv->key);

        g_byte_array_append (priv->out, (const guint8 *) G_PASTE_SECRET_STREAM_MAGIC, G_PASTE_SECRET_STREAM_MAGIC_LEN);
        g_byte_array_append (priv->out, salt, sizeof (salt));
        append_u64_le (priv->out, opslimit);
        append_u64_le (priv->out, memlimit);
        g_byte_array_append (priv->out, header, sizeof (header));

        priv->header_done = TRUE;
    }

    /* Emit full chunks while strictly more than a chunk is buffered, so the
     * trailing bytes are available to become the FINAL chunk at end. */
    while (priv->in->len > CHUNK_SIZE)
        push_chunk (priv, CHUNK_SIZE, TAG_MESSAGE);

    if (at_end && !priv->finished)
    {
        push_chunk (priv, priv->in->len, TAG_FINAL);
        priv->finished = TRUE;
    }

    return TRUE;
}

static gboolean
decrypt_process (GPasteSecretStreamConverterPrivate *priv,
                 gboolean                            at_end G_GNUC_UNUSED,
                 GError                            **error)
{
    if (!priv->header_done)
    {
        if (priv->in->len < STREAM_HEADER_LEN)
            return TRUE; /* need more input */

        const guint8 *data = priv->in->data;

        if (memcmp (data, G_PASTE_SECRET_STREAM_MAGIC, G_PASTE_SECRET_STREAM_MAGIC_LEN) != 0)
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                 "Not a GPaste encrypted stream");
            return FALSE;
        }

        const unsigned char *salt = data + G_PASTE_SECRET_STREAM_MAGIC_LEN;
        guint64 opslimit = read_u64_le (salt + SALTBYTES);
        guint64 memlimit = read_u64_le (salt + SALTBYTES + 8);
        const unsigned char *header = salt + SALTBYTES + 16;

        if (!derive_key (priv, salt, opslimit, memlimit, error))
            return FALSE;

        if (crypto_secretstream_xchacha20poly1305_init_pull (&priv->state, header, priv->key) != 0)
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                 "Corrupted encryption header");
            return FALSE;
        }

        g_byte_array_remove_range (priv->in, 0, STREAM_HEADER_LEN);
        priv->header_done = TRUE;
    }

    while (!priv->finished && priv->in->len >= 4)
    {
        guint32 clen = read_u32_le (priv->in->data);

        if (clen < ABYTES || clen > CHUNK_SIZE + ABYTES)
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                 "Corrupted encrypted stream");
            return FALSE;
        }

        if (priv->in->len < (gsize) 4 + clen)
            break; /* need the rest of the frame */

        unsigned char plain[CHUNK_SIZE];
        unsigned long long mlen = 0;
        unsigned char tag = 0;

        if (crypto_secretstream_xchacha20poly1305_pull (&priv->state, plain, &mlen, &tag,
                                                        priv->in->data + 4, clen, NULL, 0) != 0)
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                 "Could not decrypt the stream (wrong passphrase or corrupted data)");
            return FALSE;
        }

        g_byte_array_append (priv->out, plain, mlen);
        g_byte_array_remove_range (priv->in, 0, 4 + clen);

        if (tag == TAG_FINAL)
            priv->finished = TRUE;
    }

    return TRUE;
}

static GConverterResult
g_paste_secret_stream_converter_convert (GConverter      *converter,
                                         const void      *inbuf,
                                         gsize            inbuf_size,
                                         void            *outbuf,
                                         gsize            outbuf_size,
                                         GConverterFlags  flags,
                                         gsize           *bytes_read,
                                         gsize           *bytes_written,
                                         GError         **error)
{
    GPasteSecretStreamConverterPrivate *priv =
        g_paste_secret_stream_converter_get_instance_private (G_PASTE_SECRET_STREAM_CONVERTER (converter));
    gboolean at_end = (flags & G_CONVERTER_INPUT_AT_END) != 0;

    *bytes_read = 0;
    *bytes_written = 0;

    /* Buffer all the input we are handed, then process what we can. */
    if (inbuf_size)
    {
        g_byte_array_append (priv->in, inbuf, inbuf_size);
        *bytes_read = inbuf_size;
    }

    gboolean ok = (priv->direction == G_PASTE_SECRET_STREAM_ENCRYPT)
        ? encrypt_process (priv, at_end, error)
        : decrypt_process (priv, at_end, error);

    if (!ok)
        return G_CONVERTER_ERROR;

    /* Hand back as much produced output as fits. */
    if (priv->out->len && outbuf_size)
    {
        gsize n = MIN (priv->out->len, outbuf_size);

        memcpy (outbuf, priv->out->data, n);
        g_byte_array_remove_range (priv->out, 0, n);
        *bytes_written = n;
    }

    if (priv->finished && !priv->out->len)
        return G_CONVERTER_FINISHED;

    if ((flags & G_CONVERTER_FLUSH) && !priv->out->len)
        return G_CONVERTER_FLUSHED;

    if (!*bytes_read && !*bytes_written)
    {
        /* No progress: say why so the caller does not spin. */
        if (priv->out->len)
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                                 "Not enough space in the output buffer");
        else
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT,
                                 at_end ? "Truncated encrypted stream" : "Need more input");
        return G_CONVERTER_ERROR;
    }

    return G_CONVERTER_CONVERTED;
}

static void
g_paste_secret_stream_converter_reset (GConverter *converter)
{
    GPasteSecretStreamConverterPrivate *priv =
        g_paste_secret_stream_converter_get_instance_private (G_PASTE_SECRET_STREAM_CONVERTER (converter));

    priv->header_done = FALSE;
    priv->finished = FALSE;
    sodium_memzero (priv->key, KEYBYTES);
    sodium_memzero (&priv->state, sizeof (priv->state));
    g_byte_array_set_size (priv->in, 0);
    g_byte_array_set_size (priv->out, 0);
}

static void
g_paste_secret_stream_converter_iface_init (GConverterIface *iface)
{
    iface->convert = g_paste_secret_stream_converter_convert;
    iface->reset = g_paste_secret_stream_converter_reset;
}

static void
g_paste_secret_stream_converter_finalize (GObject *object)
{
    GPasteSecretStreamConverterPrivate *priv =
        g_paste_secret_stream_converter_get_instance_private (G_PASTE_SECRET_STREAM_CONVERTER (object));

    /* gcr secure memory is wiped on free. */
    gcr_secure_memory_strfree ((gchar *) priv->passphrase);
    gcr_secure_memory_free (priv->key);

    sodium_memzero (&priv->state, sizeof (priv->state));

    g_byte_array_unref (priv->in);
    g_byte_array_unref (priv->out);

    G_OBJECT_CLASS (g_paste_secret_stream_converter_parent_class)->finalize (object);
}

static void
g_paste_secret_stream_converter_class_init (GPasteSecretStreamConverterClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = g_paste_secret_stream_converter_finalize;
}

static void
g_paste_secret_stream_converter_init (GPasteSecretStreamConverter *self)
{
    GPasteSecretStreamConverterPrivate *priv = g_paste_secret_stream_converter_get_instance_private (self);

    priv->key = gcr_secure_memory_alloc (KEYBYTES);
    priv->in = g_byte_array_new ();
    priv->out = g_byte_array_new ();
}

/**
 * g_paste_secret_stream_converter_new:
 * @direction: whether to encrypt or decrypt
 * @passphrase: the passphrase the key is derived from
 *
 * Create a #GConverter that encrypts or decrypts a stream with libsodium's
 * secretstream (XChaCha20-Poly1305), deriving the key from @passphrase with
 * Argon2id. The same type handles both directions.
 *
 * Returns: (transfer full) (nullable): a newly allocated #GConverter,
 *          or %NULL if libsodium could not be initialised
 */
G_PASTE_VISIBLE GConverter *
g_paste_secret_stream_converter_new (GPasteSecretStreamDirection  direction,
                                     const gchar                 *passphrase)
{
    g_return_val_if_fail (passphrase && *passphrase, NULL);

    if (sodium_init () < 0)
    {
        g_warning ("Could not initialise libsodium");
        return NULL;
    }

    GPasteSecretStreamConverter *self = g_object_new (G_PASTE_TYPE_SECRET_STREAM_CONVERTER, NULL);
    GPasteSecretStreamConverterPrivate *priv = g_paste_secret_stream_converter_get_instance_private (self);

    priv->direction = direction;
    priv->passphrase_len = strlen (passphrase);
    priv->passphrase = (guchar *) gcr_secure_memory_strdup (passphrase);

    return G_CONVERTER (self);
}
