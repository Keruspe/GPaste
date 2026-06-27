// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-util.h>

#include <gpaste-file-backend.h>
#include <gpaste-noop-backend.h>

#ifdef G_PASTE_ENABLE_ENCRYPTION
#define GCR_API_SUBJECT_TO_CHANGE
#include <gcr/gcr.h>

/* The daemon's single master passphrase for the encrypted backend, obtained
 * once at startup (prompt or keyring). Kept process-wide in gcr secure memory
 * so every history the daemon builds resolves the encrypted backend without
 * threading the secret through every constructor. */
static gchar *g_paste_storage_passphrase = NULL;

/**
 * g_paste_storage_backend_set_passphrase:
 * @passphrase: (nullable): the master passphrase, or %NULL to clear it
 *
 * Set the passphrase used by every encrypted file backend created afterwards.
 */
G_PASTE_VISIBLE void
g_paste_storage_backend_set_passphrase (const gchar *passphrase)
{
    gcr_secure_memory_strfree (g_paste_storage_passphrase);
    g_paste_storage_passphrase = (passphrase && *passphrase) ? gcr_secure_memory_strdup (passphrase) : NULL;
}

/**
 * g_paste_storage_backend_get_passphrase:
 *
 * Returns: (nullable): the master passphrase set with
 *          g_paste_storage_backend_set_passphrase(), or %NULL
 */
G_PASTE_VISIBLE const gchar *
g_paste_storage_backend_get_passphrase (void)
{
    return g_paste_storage_passphrase;
}
#endif

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

/**
 * g_paste_storage_backend_delete_history:
 * @self: a #GPasteStorageBackend instance
 * @name: the name of the history to delete
 * @error: a #GError
 *
 * Delete a history from our storage backend
 */
G_PASTE_VISIBLE void
g_paste_storage_backend_delete_history (const GPasteStorageBackend *self,
                                         const gchar                *name,
                                         GError                   **error)
{
    g_return_if_fail (_G_PASTE_IS_STORAGE_BACKEND (self));
    g_return_if_fail (name);

    if (_G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->delete_history)
        _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->delete_history (self, name, error);
}

/**
 * g_paste_storage_backend_list_histories:
 * @self: a #GPasteStorageBackend instance
 * @error: a #GError
 *
 * Get the list of available histories from our storage backend
 *
 * Returns: (transfer full): The list of history names
 */
G_PASTE_VISIBLE GStrv
g_paste_storage_backend_list_histories (const GPasteStorageBackend *self,
                                         GError                   **error)
{
    g_return_val_if_fail (_G_PASTE_IS_STORAGE_BACKEND (self), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    if (_G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->list_histories)
        return _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->list_histories (self, error);

    return NULL;
}

/**
 * g_paste_storage_backend_add_item:
 * @self: a #GPasteStorageBackend instance
 * @name: the name of the history to update
 * @item: the #GPasteItem just added at the front of the history
 * @history: (element-type GPasteItem): the full history (used as a fallback snapshot)
 *
 * Persist a newly added item, possibly without rewriting the whole history
 */
G_PASTE_VISIBLE void
g_paste_storage_backend_add_item (const GPasteStorageBackend *self,
                                  const gchar                *name,
                                  const GPasteItem           *item,
                                  const GList                *history)
{
    g_return_if_fail (_G_PASTE_IS_STORAGE_BACKEND (self));
    g_return_if_fail (name);

    if (_G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->add_item)
        _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->add_item (self, name, item, history);
    else
        g_paste_storage_backend_write_history (self, name, history);
}

/**
 * g_paste_storage_backend_remove_item:
 * @self: a #GPasteStorageBackend instance
 * @name: the name of the history to update
 * @uuid: the uuid of the removed item
 * @history: (element-type GPasteItem): the full history (used as a fallback snapshot)
 *
 * Persist the removal of an item, possibly without rewriting the whole history
 */
G_PASTE_VISIBLE void
g_paste_storage_backend_remove_item (const GPasteStorageBackend *self,
                                     const gchar                *name,
                                     const gchar                *uuid,
                                     const GList                *history)
{
    g_return_if_fail (_G_PASTE_IS_STORAGE_BACKEND (self));
    g_return_if_fail (name);

    if (_G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->remove_item)
        _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->remove_item (self, name, uuid);
    else
        g_paste_storage_backend_write_history (self, name, history);
}

/**
 * g_paste_storage_backend_replace_item:
 * @self: a #GPasteStorageBackend instance
 * @name: the name of the history to update
 * @old_uuid: the uuid of the item being replaced
 * @item: the #GPasteItem taking its place
 * @history: (element-type GPasteItem): the full history (used as a fallback snapshot)
 *
 * Persist an item replacement, possibly without rewriting the whole history
 */
G_PASTE_VISIBLE void
g_paste_storage_backend_replace_item (const GPasteStorageBackend *self,
                                      const gchar                *name,
                                      const gchar                *old_uuid,
                                      const GPasteItem           *item,
                                      const GList                *history)
{
    g_return_if_fail (_G_PASTE_IS_STORAGE_BACKEND (self));
    g_return_if_fail (name);
    g_return_if_fail (old_uuid);

    if (_G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->replace_item)
        _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->replace_item (self, name, old_uuid, item);
    else
        g_paste_storage_backend_write_history (self, name, history);
}

/**
 * g_paste_storage_backend_clear_history:
 * @self: a #GPasteStorageBackend instance
 * @name: the name of the history to clear
 * @history: (element-type GPasteItem): the (now empty) full history
 *
 * Persist the emptying of a history, possibly without rewriting the whole file
 */
G_PASTE_VISIBLE void
g_paste_storage_backend_clear_history (const GPasteStorageBackend *self,
                                       const gchar                *name,
                                       const GList                *history)
{
    g_return_if_fail (_G_PASTE_IS_STORAGE_BACKEND (self));
    g_return_if_fail (name);

    if (_G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->clear_history)
        _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->clear_history (self, name);
    else
        g_paste_storage_backend_write_history (self, name, history);
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
    klass->delete_history = NULL;
    klass->list_histories = NULL;

    klass->add_item = NULL;
    klass->remove_item = NULL;
    klass->replace_item = NULL;
    klass->clear_history = NULL;

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
    case G_PASTE_STORAGE_NOOP:
        return G_PASTE_TYPE_NOOP_BACKEND;
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

#ifdef G_PASTE_ENABLE_ENCRYPTION
    if (storage_kind == G_PASTE_STORAGE_ENCRYPTED_FILE)
    {
        const gchar *passphrase = g_paste_storage_backend_get_passphrase ();

        if (passphrase)
            return g_paste_file_backend_new_encrypted (settings, passphrase);

        /* Without a passphrase we must not fall back to plaintext on disk;
         * keep the history in memory only. */
        g_warning ("No passphrase for the encrypted storage backend; not storing the history");
        storage_kind = G_PASTE_STORAGE_NOOP;
    }
#endif

    GPasteStorageBackend *self = g_object_new (_g_paste_storage_backend_get_type (storage_kind), NULL);
    GPasteStorageBackendPrivate *priv = g_paste_storage_backend_get_instance_private (self);

    priv->settings = g_object_ref (settings);

    return self;
}
