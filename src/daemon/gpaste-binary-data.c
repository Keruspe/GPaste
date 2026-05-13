/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-binary-data.h>

struct _GPasteBinaryData
{
    GObject parent_instance;
};

typedef struct
{
    GPasteSpecialAtom  mime;
    guchar            *data;
    gsize              length;
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
 * g_paste_binary_data_get_data:
 * @self: a #GPasteBinaryData instance
 *
 * Get the raw bytes stored in the #GPasteBinaryData
 *
 * Returns: read-only pointer to the raw bytes
 */
G_PASTE_VISIBLE const guchar *
g_paste_binary_data_get_data (const GPasteBinaryData *self)
{
    g_return_val_if_fail (_G_PASTE_IS_BINARY_DATA (self), NULL);

    const GPasteBinaryDataPrivate *priv = _g_paste_binary_data_get_instance_private (self);

    return priv->data;
}

/**
 * g_paste_binary_data_get_length:
 * @self: a #GPasteBinaryData instance
 *
 * Get the number of bytes stored in the #GPasteBinaryData
 *
 * Returns: the length in bytes
 */
G_PASTE_VISIBLE gsize
g_paste_binary_data_get_length (const GPasteBinaryData *self)
{
    g_return_val_if_fail (_G_PASTE_IS_BINARY_DATA (self), 0);

    const GPasteBinaryDataPrivate *priv = _g_paste_binary_data_get_instance_private (self);

    return priv->length;
}

/**
 * g_paste_binary_data_get_checksum:
 * @self: a #GPasteBinaryData instance
 *
 * Get the SHA256 checksum of the data stored in the #GPasteBinaryData
 *
 * Returns: read-only string containing the SHA256 checksum
 */
G_PASTE_VISIBLE const gchar *
g_paste_binary_data_get_checksum (const GPasteBinaryData *self)
{
    g_return_val_if_fail (_G_PASTE_IS_BINARY_DATA (self), NULL);

    const GPasteBinaryDataPrivate *priv = _g_paste_binary_data_get_instance_private (self);

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

    return g_base64_encode (priv->data, priv->length);
}

static void
g_paste_binary_data_finalize (GObject *object)
{
    const GPasteBinaryDataPrivate *priv = _g_paste_binary_data_get_instance_private (G_PASTE_BINARY_DATA (object));

    g_free (priv->data);
    g_free (priv->checksum);

    G_OBJECT_CLASS (g_paste_binary_data_parent_class)->finalize (object);
}

static void
g_paste_binary_data_class_init (GPasteBinaryDataClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = g_paste_binary_data_finalize;
}

static void
g_paste_binary_data_init (GPasteBinaryData *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_binary_data_new:
 * @mime: the #GPasteSpecialAtom identifying the mime type
 * @data: the raw bytes to store
 * @length: the number of bytes in @data
 *
 * Create a new instance of #GPasteBinaryData, copying @data and computing its SHA256 checksum
 *
 * Returns: a newly allocated #GPasteBinaryData
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteBinaryData *
g_paste_binary_data_new (GPasteSpecialAtom  mime,
                         const guchar      *data,
                         gsize              length)
{
    g_return_val_if_fail (data, NULL);
    g_return_val_if_fail (length > 0, NULL);

    GPasteBinaryData *self = g_object_new (G_PASTE_TYPE_BINARY_DATA, NULL);
    GPasteBinaryDataPrivate *priv = g_paste_binary_data_get_instance_private (self);

    priv->mime = mime;
    priv->data = g_memdup2 (data, length);
    priv->length = length;
    priv->checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA256, data, length);

    return self;
}
