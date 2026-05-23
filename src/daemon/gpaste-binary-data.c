// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-binary-data.h>

struct _GPasteBinaryData
{
    GObject parent_instance;
};

typedef struct
{
    GPasteSpecialAtom  mime;
    GBytes            *bytes;
    gchar             *checksum;
} GPasteBinaryDataPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (BinaryData, binary_data, G_TYPE_OBJECT)

/**
 * g_paste_binary_data_get_mime:
 * @self: a #GPasteBinaryData instance
 *
 * Get the special atom (mime type) associated with the #GPasteBinaryData
 *
 * Returns: the #GPasteSpecialAtom
 */
G_PASTE_VISIBLE GPasteSpecialAtom
g_paste_binary_data_get_mime (const GPasteBinaryData *self)
{
    g_return_val_if_fail (_G_PASTE_IS_BINARY_DATA (self), G_PASTE_SPECIAL_ATOM_INVALID);

    const GPasteBinaryDataPrivate *priv = _g_paste_binary_data_get_instance_private (self);

    return priv->mime;
}

/**
 * g_paste_binary_data_get_bytes:
 * @self: a #GPasteBinaryData instance
 *
 * Get the bytes stored in the #GPasteBinaryData
 *
 * Returns: (transfer none): read-only #GBytes
 */
G_PASTE_VISIBLE GBytes *
g_paste_binary_data_get_bytes (const GPasteBinaryData *self)
{
    g_return_val_if_fail (_G_PASTE_IS_BINARY_DATA (self), NULL);

    const GPasteBinaryDataPrivate *priv = _g_paste_binary_data_get_instance_private (self);

    return priv->bytes;
}

/**
 * g_paste_binary_data_get_checksum:
 * @self: a #GPasteBinaryData instance
 *
 * Get the SHA256 checksum of the data stored in the #GPasteBinaryData.
 * Computed lazily on first call.
 *
 * Returns: read-only string containing the SHA256 checksum
 */
G_PASTE_VISIBLE const gchar *
g_paste_binary_data_get_checksum (const GPasteBinaryData *self)
{
    g_return_val_if_fail (_G_PASTE_IS_BINARY_DATA (self), NULL);

    GPasteBinaryDataPrivate *priv = g_paste_binary_data_get_instance_private ((GPasteBinaryData *)(gpointer) self);

    if (!priv->checksum)
    {
        gsize len;
        const guchar *data = g_bytes_get_data (priv->bytes, &len);
        priv->checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA256, data, len);
    }

    return priv->checksum;
}

/**
 * g_paste_binary_data_to_base64:
 * @self: a #GPasteBinaryData instance
 *
 * Encode the data stored in the #GPasteBinaryData as a base64 string
 *
 * Returns: a newly allocated base64-encoded string
 *          free it with g_free
 */
G_PASTE_VISIBLE gchar *
g_paste_binary_data_to_base64 (const GPasteBinaryData *self)
{
    g_return_val_if_fail (_G_PASTE_IS_BINARY_DATA (self), NULL);

    const GPasteBinaryDataPrivate *priv = _g_paste_binary_data_get_instance_private (self);

    gsize len;
    const guchar *data = g_bytes_get_data (priv->bytes, &len);

    return g_base64_encode (data, len);
}

static void
g_paste_binary_data_dispose (GObject *object)
{
    GPasteBinaryDataPrivate *priv = g_paste_binary_data_get_instance_private (G_PASTE_BINARY_DATA (object));

    g_clear_pointer (&priv->bytes, g_bytes_unref);

    G_OBJECT_CLASS (g_paste_binary_data_parent_class)->dispose (object);
}

static void
g_paste_binary_data_finalize (GObject *object)
{
    GPasteBinaryDataPrivate *priv = g_paste_binary_data_get_instance_private (G_PASTE_BINARY_DATA (object));

    g_free (priv->checksum);

    G_OBJECT_CLASS (g_paste_binary_data_parent_class)->finalize (object);
}

static void
g_paste_binary_data_class_init (GPasteBinaryDataClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_binary_data_dispose;
    G_OBJECT_CLASS (klass)->finalize = g_paste_binary_data_finalize;
}

static void
g_paste_binary_data_init (GPasteBinaryData *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_binary_data_new:
 * @mime: the #GPasteSpecialAtom identifying the mime type
 * @bytes: (transfer full): the bytes to store
 *
 * Create a new instance of #GPasteBinaryData, taking ownership of @bytes
 *
 * Returns: a newly allocated #GPasteBinaryData
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteBinaryData *
g_paste_binary_data_new (GPasteSpecialAtom  mime,
                         GBytes            *bytes)
{
    g_return_val_if_fail (bytes, NULL);
    g_return_val_if_fail (g_bytes_get_size (bytes) > 0, NULL);

    GPasteBinaryData *self = g_object_new (G_PASTE_TYPE_BINARY_DATA, NULL);
    GPasteBinaryDataPrivate *priv = g_paste_binary_data_get_instance_private (self);

    priv->mime = mime;
    priv->bytes = bytes;

    return self;
}
