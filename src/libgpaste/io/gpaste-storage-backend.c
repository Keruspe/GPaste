/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-file-backend.h>

typedef struct
{
    gchar *source;
} GPasteStorageBackendPrivate;

G_PASTE_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (StorageBackend, storage_backend, G_TYPE_OBJECT)

/**
 * g_paste_storage_backend_read_history:
 * @self: a #GPasteItem instance
 *
 * Reads the history from our storage backend
 *
 * Returns: the saved history
 */
G_PASTE_VISIBLE GList *
g_paste_storage_backend_read_history (const GPasteStorageBackend *self)
{
    g_return_val_if_fail (_G_PASTE_IS_STORAGE_BACKEND (self), NULL);

    const GPasteStorageBackendPrivate *priv = _g_paste_storage_backend_get_instance_private (self);

    return _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->read_history (self, priv->source);
}

/**
 * g_paste_storage_backend_write_history:
 * @self: a #GPasteItem instance
 * @history: the history to write
 *
 * Save the history by writing it to our storage backend
 */
G_PASTE_VISIBLE void
g_paste_storage_backend_write_history (const GPasteStorageBackend *self,
                                       const GList                *history)
{
    g_return_if_fail (_G_PASTE_IS_STORAGE_BACKEND (self));

    const GPasteStorageBackendPrivate *priv = _g_paste_storage_backend_get_instance_private (self);

    _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->write_history (self, priv->source, history);
}

static void
g_paste_storage_backend_finalize (GObject *object)
{
    const GPasteStorageBackendPrivate *priv = _g_paste_storage_backend_get_instance_private (G_PASTE_STORAGE_BACKEND (object));

    g_free (priv->source);

    G_OBJECT_CLASS (g_paste_storage_backend_parent_class)->finalize (object);
}

static void
g_paste_storage_backend_class_init (GPasteStorageBackendClass *klass)
{
    klass->read_history = NULL;
    klass->write_history = NULL;

    G_OBJECT_CLASS (klass)->finalize = g_paste_storage_backend_finalize;
}

static void
g_paste_storage_backend_init (GPasteStorageBackend *self G_GNUC_UNUSED)
{
}

static GType
_g_paste_storage_backend_get_type (GPasteStorage storage_kind)
{
    switch (storage_kind)
    {
    case G_PASTE_STORAGE_FILE:
        return G_PASTE_TYPE_FILE_BACKEND;
    default:
        return _g_paste_storage_backend_get_type (G_PASTE_STORAGE_DEFAULT);
    }
}

/**
 * g_paste_storage_backend_new:
 * @storage_kind: the kind of storage we want to use to save and load history
 * @source: the location where the storage is at
 *
 * Create a new instance of #GPasteStorageBackend
 *
 * Returns: a newly allocated #GPasteStorageBackend
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteStorageBackend *
g_paste_storage_backend_new (GPasteStorage storage_kind,
                             const gchar  *source)
{
    g_return_val_if_fail (source, NULL);

    GPasteStorageBackend *self = g_object_new (_g_paste_storage_backend_get_type (storage_kind), NULL);
    GPasteStorageBackendPrivate *priv = g_paste_storage_backend_get_instance_private (self);

    priv->source = g_strdup (source);

    return self;
}
