/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste/gpaste-util.h>

#include <gpaste-file-backend.h>

typedef struct
{
    GPasteSettings *settings;
} GPasteStorageBackendPrivate;

G_PASTE_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (StorageBackend, storage_backend, G_TYPE_OBJECT)

static gchar *
_g_paste_storage_backend_get_history_file_path (const GPasteStorageBackend *self,
                                                const gchar                *name)
{
    return g_paste_util_get_history_file_path (name, _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->get_extension (self));
}

/**
 * g_paste_storage_backend_read_history:
 * @self: a #GPasteItem instance
 * @name: the name of the history to load
 * @history: (out) (element-type GPasteItem): the history we just read
 * @size: (out): the size used by the history
 *
 * Reads the history from our storage backend
 */
G_PASTE_VISIBLE void
g_paste_storage_backend_read_history (const GPasteStorageBackend *self,
                                      const gchar                *name,
                                      GList                     **history,
                                      gsize                      *size)
{
    g_return_if_fail (_G_PASTE_IS_STORAGE_BACKEND (self));
    g_return_if_fail (name);
    g_return_if_fail (history && !*history);
    g_return_if_fail (size);

    g_autofree gchar *history_file_path = _g_paste_storage_backend_get_history_file_path (self, name);

    _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->read_history_file (self, history_file_path, history, size);
}

/**
 * g_paste_storage_backend_write_history:
 * @self: a #GPasteItem instance
 * @name: the name of the history to save
 * @history: (element-type GPasteItem): the history to write
 *
 * Save the history by writing it to our storage backend
 */
G_PASTE_VISIBLE void
g_paste_storage_backend_write_history (const GPasteStorageBackend *self,
                                       const gchar                *name,
                                       const GList                *history)
{
    g_return_if_fail (_G_PASTE_IS_STORAGE_BACKEND (self));

    g_autofree gchar *history_file_path = _g_paste_storage_backend_get_history_file_path (self, name);

    _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->write_history_file (self, history_file_path, history);
}

static void
g_paste_storage_backend_dispose (GObject *object)
{
    GPasteStorageBackendPrivate *priv = g_paste_storage_backend_get_instance_private (G_PASTE_STORAGE_BACKEND (object));

    g_clear_object (&priv->settings);

    G_OBJECT_CLASS (g_paste_storage_backend_parent_class)->dispose (object);
}

static const GPasteSettings *
g_paste_storage_backend_get_settings (const GPasteStorageBackend *self)
{
    const GPasteStorageBackendPrivate *priv = _g_paste_storage_backend_get_instance_private (self);

    return priv->settings;
}

static void
g_paste_storage_backend_class_init (GPasteStorageBackendClass *klass)
{
    klass->read_history_file = NULL;
    klass->write_history_file = NULL;
    klass->get_extension = NULL;
    klass->get_settings = g_paste_storage_backend_get_settings;

    G_OBJECT_CLASS (klass)->dispose = g_paste_storage_backend_dispose;
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
 * @settings: a #GPasteSettings instance
 *
 * Create a new instance of #GPasteStorageBackend
 *
 * Returns: a newly allocated #GPasteStorageBackend
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteStorageBackend *
g_paste_storage_backend_new (GPasteStorage   storage_kind,
                             GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    GPasteStorageBackend *self = g_object_new (_g_paste_storage_backend_get_type (storage_kind), NULL);
    GPasteStorageBackendPrivate *priv = g_paste_storage_backend_get_instance_private (self);

    priv->settings = g_object_ref (settings);

    return self;
}
