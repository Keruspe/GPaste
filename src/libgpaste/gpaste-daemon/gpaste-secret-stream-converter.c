// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-secret-stream-converter.h>

#include <glib/gstdio.h>

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

typedef crypto_secretstream_xchacha20poly1305_state StreamState;
#define FRAME_LEN         (4 + CHUNK_SIZE + ABYTES)

/* What passes through the converter is the plaintext on one side, so it is
 * held in buffers of a fixed capacity in gcr secure memory -- never swapped
 * out, and wiped when freed -- and never grown: a buffer reallocated leaves a
 * copy of what it held in every block it moves out of. Consuming moves the
 * rest to the front and wipes what that leaves behind. The input takes what
 * fits, which a GConverter may do, and the output is produced a frame at a
 * time while there is room: a chunk and the next byte to tell the last chunk
 * apart going in, the header and a frame coming out.
 *
 * No test tells these from ordinary buffers: consuming wipes what it leaves, so
 * a freed one holds no plaintext either way, a block realloc () moves out of is
 * freed inside libc where no interposed free () sees it, and memory kept off
 * the swap shows nowhere a test can read. */
#define IN_CAPACITY  (FRAME_LEN + CHUNK_SIZE)
#define OUT_CAPACITY (STREAM_HEADER_LEN + FRAME_LEN)

typedef struct
{
    guint8 *data;
    gsize   len;
    gsize   capacity;
} SecureBuffer;

static void
secure_buffer_init (SecureBuffer *buffer,
                    gsize         capacity)
{
    buffer->data = gcr_secure_memory_alloc (capacity);
    buffer->len = 0;
    buffer->capacity = capacity;
}

static gsize
secure_buffer_room (const SecureBuffer *buffer)
{
    return buffer->capacity - buffer->len;
}

static void
secure_buffer_append (SecureBuffer *buffer,
                      gconstpointer data,
                      gsize         len)
{
    g_assert (len <= secure_buffer_room (buffer));

    memcpy (buffer->data + buffer->len, data, len);
    buffer->len += len;
}

static void
secure_buffer_consume (SecureBuffer *buffer,
                       gsize         len)
{
    gsize left = buffer->len - len;

    memmove (buffer->data, buffer->data + len, left);
    sodium_memzero (buffer->data + left, len);
    buffer->len = left;
}

static void
secure_buffer_clear (SecureBuffer *buffer)
{
    sodium_memzero (buffer->data, buffer->len);
    buffer->len = 0;
}

struct _GPasteSecretStreamConverter
{
    GObject parent_instance;

    GPasteSecretStreamDirection direction;
    /* Kept (in gcr secure memory) for the converter's lifetime: the key is
     * derived lazily and salt-dependent, so reset() must be able to re-derive
     * it for a fresh stream (whose salt may differ). */
    guchar                     *passphrase;
    gsize                       passphrase_len;

    gboolean                    header_done; /* header emitted (encrypt) or parsed (decrypt) */
    gboolean                    finished;    /* FINAL chunk produced/consumed */
    gboolean                    opened;      /* a chunk decrypted: the key is the stream's */
    gboolean                    ends_at_final; /* what follows the FINAL chunk is left, not refused */

    /* In gcr secure (non-swappable) memory: the state holds a subkey of @key. */
    StreamState                *state;
    guchar                     *key; /* KEYBYTES */

    SecureBuffer                in;  /* input not processed yet */
    SecureBuffer                out; /* output not handed back yet */
};

static void g_paste_secret_stream_converter_iface_init (GConverterIface *iface);

G_PASTE_DEFINE_TYPE_WITH_INTERFACE (SecretStreamConverter, secret_stream_converter, G_TYPE_OBJECT,
                                    G_TYPE_CONVERTER, g_paste_secret_stream_converter_iface_init)

static void
append_u32_le (SecureBuffer *buffer,
               guint32       value)
{
    guint32 le = GUINT32_TO_LE (value);

    secure_buffer_append (buffer, &le, sizeof (le));
}

static void
append_u64_le (SecureBuffer *buffer,
               guint64       value)
{
    guint64 le = GUINT64_TO_LE (value);

    secure_buffer_append (buffer, &le, sizeof (le));
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
push_chunk (GPasteSecretStreamConverter *self,
            gsize                        len,
            unsigned char                tag)
{
    unsigned char cipher[CHUNK_SIZE + ABYTES];
    unsigned long long clen = 0;

    crypto_secretstream_xchacha20poly1305_push (self->state, cipher, &clen,
                                                self->in.data, len, NULL, 0, tag);

    append_u32_le (&self->out, (guint32) clen);
    secure_buffer_append (&self->out, cipher, clen);

    if (len)
        secure_buffer_consume (&self->in, len);
}

/**
 * g_paste_crypto_derive_key:
 * @passphrase: the passphrase to derive from
 * @passphrase_len: its length
 * @salt: crypto_pwhash_SALTBYTES of salt
 * @opslimit: the Argon2 operations limit the store asks for
 * @memlimit: the Argon2 memory limit the store asks for
 * @key: (out caller-allocates): where to write @key_len bytes of key
 * @key_len: how much key to derive
 * @error: return location for a #GError, or %NULL
 *
 * Derive a key with Argon2id, refusing parameters no GPaste ever wrote.
 *
 * Every caller reads @opslimit and @memlimit back from the store -- an
 * encrypted stream's header, or the SQLite `meta` table -- so both are
 * attacker-controlled for anything that can write to $XDG_DATA_HOME. A huge
 * opslimit would make crypto_pwhash run effectively forever: a denial of
 * service just from opening a history. We only ever write MODERATE parameters,
 * so anything beyond SENSITIVE is corrupt or tampered with. Deriving through
 * here is what keeps that bound from being forgotten at a new call site.
 *
 * Fails with %G_IO_ERROR_INVALID_DATA for out-of-range parameters and
 * %G_IO_ERROR_FAILED when the derivation itself does not complete.
 *
 * Returns: whether @key was derived
 */
G_PASTE_VISIBLE gboolean
g_paste_crypto_derive_key (const gchar  *passphrase,
                           gsize         passphrase_len,
                           const guchar *salt,
                           guint64       opslimit,
                           guint64       memlimit,
                           guchar       *key,
                           gsize         key_len,
                           GError      **error)
{
    g_return_val_if_fail (passphrase, FALSE);
    g_return_val_if_fail (salt, FALSE);
    g_return_val_if_fail (key, FALSE);

    if (opslimit > crypto_pwhash_OPSLIMIT_SENSITIVE || memlimit > crypto_pwhash_MEMLIMIT_SENSITIVE)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "Unreasonable key-derivation parameters");
        return FALSE;
    }

    if (crypto_pwhash (key, key_len,
                       passphrase, passphrase_len,
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
derive_key (GPasteSecretStreamConverter *self,
            const unsigned char         *salt,
            guint64                      opslimit,
            guint64                      memlimit,
            GError                     **error)
{
    return g_paste_crypto_derive_key ((const gchar *) self->passphrase, self->passphrase_len,
                                      salt, opslimit, memlimit,
                                      self->key, KEYBYTES, error);
}

static gboolean
encrypt_process (GPasteSecretStreamConverter *self,
                 gboolean                     flush,
                 gboolean                     at_end,
                 GError                     **error)
{
    if (!self->header_done)
    {
        unsigned char salt[SALTBYTES];
        unsigned char header[HEADERBYTES];
        guint64 opslimit = OPSLIMIT;
        guint64 memlimit = MEMLIMIT;

        randombytes_buf (salt, sizeof (salt));

        if (!derive_key (self, salt, opslimit, memlimit, error))
            return FALSE;

        crypto_secretstream_xchacha20poly1305_init_push (self->state, header, self->key);

        secure_buffer_append (&self->out, G_PASTE_SECRET_STREAM_MAGIC, G_PASTE_SECRET_STREAM_MAGIC_LEN);
        secure_buffer_append (&self->out, salt, sizeof (salt));
        append_u64_le (&self->out, opslimit);
        append_u64_le (&self->out, memlimit);
        secure_buffer_append (&self->out, header, sizeof (header));

        self->header_done = TRUE;
    }

    /* Emit full chunks while strictly more than a chunk is buffered, so the
     * trailing bytes are available to become the FINAL chunk at end -- each
     * while the output has room for its frame. */
    while (self->in.len > CHUNK_SIZE && secure_buffer_room (&self->out) >= FRAME_LEN)
        push_chunk (self, CHUNK_SIZE, TAG_MESSAGE);

    if (self->in.len > CHUNK_SIZE || secure_buffer_room (&self->out) < FRAME_LEN)
        return TRUE;

    /* On an explicit flush, also emit the buffered partial chunk so all the input
     * we were handed reaches the base stream (the GConverter flush contract); a
     * later write simply continues the stream and at_end still emits FINAL. */
    if (flush && !at_end && self->in.len)
        push_chunk (self, self->in.len, TAG_MESSAGE);

    if (at_end && !self->finished)
    {
        push_chunk (self, self->in.len, TAG_FINAL);
        self->finished = TRUE;
    }

    return TRUE;
}

static gboolean
decrypt_process (GPasteSecretStreamConverter *self,
                 GError                     **error)
{
    if (!self->header_done)
    {
        if (self->in.len < STREAM_HEADER_LEN)
            return TRUE; /* need more input */

        const guint8 *data = self->in.data;

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

        /* derive_key() is what refuses the parameters this untrusted header asks
         * for; see g_paste_crypto_derive_key(). */
        if (!derive_key (self, salt, opslimit, memlimit, error))
            return FALSE;

        if (crypto_secretstream_xchacha20poly1305_init_pull (self->state, header, self->key) != 0)
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                 "Corrupted encryption header");
            return FALSE;
        }

        secure_buffer_consume (&self->in, STREAM_HEADER_LEN);
        self->header_done = TRUE;
    }

    while (!self->finished && self->in.len >= 4)
    {
        guint32 clen = read_u32_le (self->in.data);

        if (clen < ABYTES || clen > CHUNK_SIZE + ABYTES)
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                 "Corrupted encrypted stream");
            return FALSE;
        }

        if (self->in.len < (gsize) 4 + clen)
            break; /* need the rest of the frame */

        if (secure_buffer_room (&self->out) < clen - ABYTES)
            break; /* hand the last chunk out first */

        unsigned char plain[CHUNK_SIZE];
        unsigned long long mlen = 0;
        unsigned char tag = 0;

        /* Only the first chunk failing says anything about the key: once one
         * has decrypted, the key is the stream's and what fails is the data.
         * %G_IO_ERROR_PERMISSION_DENIED is the answer a wrong passphrase gets,
         * and %G_IO_ERROR_INVALID_DATA every other -- a foreign file, a length
         * or a parameter out of range, a chunk damaged further on -- which says
         * nothing of the passphrase (see history_refutes_passphrase in
         * #GPasteStorageBackendClass). What the first chunk failing cannot tell
         * apart is a wrong key from damage to it or to the header before it, a
         * salt or a parameter within range: the stream carries no key check of
         * its own, as the SQLite flavour's database does, so such damage to a
         * history refuses the right passphrase until the file goes, and only a
         * change of format could say otherwise. */
        if (crypto_secretstream_xchacha20poly1305_pull (self->state, plain, &mlen, &tag,
                                                        self->in.data + 4, clen, NULL, 0) != 0)
        {
            if (self->opened)
                g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Corrupted encrypted stream");
            else
            {
                g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                                     "Could not decrypt the stream (wrong passphrase or corrupted data)");
            }
            return FALSE;
        }

        self->opened = TRUE;

        secure_buffer_append (&self->out, plain, mlen);
        /* The chunk is in @out now; the stack it was decrypted on keeps whatever
         * the deepest chunk of this stream left there otherwise. */
        sodium_memzero (plain, mlen);
        secure_buffer_consume (&self->in, 4 + clen);

        if (tag == TAG_FINAL)
            self->finished = TRUE;
    }

    /* The FINAL tag is what catches a stream cut short; a stream carried on
     * past it -- a sync tool appending a second version, a shorter file written
     * over a longer one without truncating it -- is caught here, rather than
     * read as its first stream alone, whatever the rest held dropped by the
     * next save. */
    if (self->finished && self->in.len)
    {
        if (self->ends_at_final)
        {
            secure_buffer_clear (&self->in);
            return TRUE;
        }

        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "Data past the end of the encrypted stream");
        return FALSE;
    }

    return TRUE;
}

static GConverterResult
g_paste_secret_stream_converter_convert (GConverter     *converter,
                                         const void     *inbuf,
                                         gsize           inbuf_size,
                                         void           *outbuf,
                                         gsize           outbuf_size,
                                         GConverterFlags flags,
                                         gsize          *bytes_read,
                                         gsize          *bytes_written,
                                         GError        **error)
{
    GPasteSecretStreamConverter *self =
        G_PASTE_SECRET_STREAM_CONVERTER (converter);

    *bytes_read = MIN (inbuf_size, secure_buffer_room (&self->in));
    *bytes_written = 0;

    /* Take what fits, then process what we can: input left over is handed in
     * again, so the end of the input, and a flush, only stand once the last of
     * it is in. */
    if (*bytes_read)
        secure_buffer_append (&self->in, inbuf, *bytes_read);

    gboolean all_in = (*bytes_read == inbuf_size);
    gboolean at_end = all_in && (flags & G_CONVERTER_INPUT_AT_END);
    gboolean flush = all_in && (flags & G_CONVERTER_FLUSH);
    gboolean ok = (self->direction == G_PASTE_SECRET_STREAM_ENCRYPT)
        ? encrypt_process (self, flush, at_end, error)
        : decrypt_process (self, error);

    if (!ok)
        return G_CONVERTER_ERROR;

    /* Hand back as much produced output as fits, then process again: what the
     * output had no room for may now fit. */
    while (self->out.len && *bytes_written < outbuf_size)
    {
        gsize n = MIN (self->out.len, outbuf_size - *bytes_written);

        memcpy ((guint8 *) outbuf + *bytes_written, self->out.data, n);
        secure_buffer_consume (&self->out, n);
        *bytes_written += n;

        ok = (self->direction == G_PASTE_SECRET_STREAM_ENCRYPT)
            ? encrypt_process (self, flush, at_end, error)
            : decrypt_process (self, error);

        if (!ok)
            return G_CONVERTER_ERROR;
    }

    /* Finished once nothing more can come: when decrypting, input past the
     * FINAL frame is an error, which only the end of the input rules out. */
    if (self->finished && !self->out.len && (at_end || self->direction == G_PASTE_SECRET_STREAM_ENCRYPT))
        return G_CONVERTER_FINISHED;

    if (flush && !self->out.len && !self->in.len)
        return G_CONVERTER_FLUSHED;

    if (!*bytes_read && !*bytes_written)
    {
        /* No progress: say why so the caller does not spin. */
        if (self->out.len)
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                                 "Not enough space in the output buffer");
        }
        else
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT,
                                 at_end ? "Truncated encrypted stream" : "Need more input");
        }
        return G_CONVERTER_ERROR;
    }

    return G_CONVERTER_CONVERTED;
}

static void
g_paste_secret_stream_converter_reset (GConverter *converter)
{
    GPasteSecretStreamConverter *self =
        G_PASTE_SECRET_STREAM_CONVERTER (converter);

    self->header_done = FALSE;
    self->finished = FALSE;
    self->opened = FALSE;
    sodium_memzero (self->key, KEYBYTES);
    sodium_memzero (self->state, sizeof (*self->state));
    secure_buffer_clear (&self->in);
    secure_buffer_clear (&self->out);
}

static void
g_paste_secret_stream_converter_iface_init (GConverterIface *iface)
{
    iface->convert = g_paste_secret_stream_converter_convert;
    iface->reset = g_paste_secret_stream_converter_reset;
}

/**
 * g_paste_secret_stream_converter_end_at_final:
 * @self: a decrypting #GPasteSecretStreamConverter
 *
 * Have @self finish at the FINAL chunk, leaving what follows it unread rather
 * than refusing the stream over it. For a stream whose plaintext something
 * authenticated elsewhere vouches for -- an image named by the checksum its
 * history holds -- where a sync tool appending a second version, or a shorter
 * file written over a longer one, would otherwise cost the whole of it.
 */
void
g_paste_secret_stream_converter_end_at_final (GPasteSecretStreamConverter *self)
{
    g_return_if_fail (G_PASTE_IS_SECRET_STREAM_CONVERTER (self));

    self->ends_at_final = TRUE;
}

/**
 * g_paste_secret_stream_file_has_frame:
 * @path: the file to look into
 *
 * Whether the file at @path holds a stream's header and the whole first frame
 * past it: what a key is told wrong by, only the stream's own key
 * authenticating that frame. An empty placeholder, a foreign file and one cut
 * short in its first frame hold none, and refute no passphrase.
 *
 * Returns: whether a wrong passphrase would be refused over the file
 */
gboolean
g_paste_secret_stream_file_has_frame (const gchar *path)
{
    g_autoptr (GFile) file = g_file_new_for_path (path);
    g_autoptr (GFileInputStream) stream = g_file_read (file, NULL, NULL);
    guchar head[STREAM_HEADER_LEN + 4];
    gsize length = 0;
    GStatBuf stat_buf;

    if (!stream || !g_input_stream_read_all (G_INPUT_STREAM (stream), head, sizeof (head), &length, NULL, NULL) ||
        length < sizeof (head) || memcmp (head, G_PASTE_SECRET_STREAM_MAGIC, G_PASTE_SECRET_STREAM_MAGIC_LEN) ||
        g_stat (path, &stat_buf))
        return FALSE;

    guint32 clen = read_u32_le (head + STREAM_HEADER_LEN);

    return clen >= ABYTES && clen <= CHUNK_SIZE + ABYTES && (guint64) stat_buf.st_size >= sizeof (head) + clen;
}

static void
g_paste_secret_stream_converter_finalize (GObject *object)
{
    GPasteSecretStreamConverter *self =
        G_PASTE_SECRET_STREAM_CONVERTER (object);

    /* gcr wipes a string it frees, whatever memory it was in. */
    gcr_secure_memory_strfree ((gchar *) self->passphrase);
    g_paste_util_secure_free (self->key, KEYBYTES);
    g_paste_util_secure_free (self->state, sizeof (*self->state));
    g_paste_util_secure_free (self->in.data, self->in.capacity);
    g_paste_util_secure_free (self->out.data, self->out.capacity);

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
    self->key = gcr_secure_memory_alloc (KEYBYTES);
    self->state = gcr_secure_memory_alloc (sizeof (*self->state));
    secure_buffer_init (&self->in, IN_CAPACITY);
    secure_buffer_init (&self->out, OUT_CAPACITY);
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
g_paste_secret_stream_converter_new (GPasteSecretStreamDirection direction,
                                     const gchar                *passphrase)
{
    g_return_val_if_fail (passphrase && *passphrase, NULL);

    if (sodium_init () < 0)
    {
        g_warning ("Could not initialise libsodium");
        return NULL;
    }

    GPasteSecretStreamConverter *self = g_object_new (G_PASTE_TYPE_SECRET_STREAM_CONVERTER, NULL);

    self->direction = direction;
    self->passphrase_len = strlen (passphrase);
    self->passphrase = (guchar *) gcr_secure_memory_strdup (passphrase);

    return G_CONVERTER (self);
}
