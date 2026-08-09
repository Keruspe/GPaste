// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-util.h>

#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-file-backend.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-noop-backend.h>

#ifdef G_PASTE_ENABLE_SQLITE
#include <gpaste-daemon/gpaste-sqlite-backend.h>
#endif

#ifdef G_OS_UNIX
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#ifdef G_PASTE_ENABLE_LIBSECRET
#include <gpaste-daemon/gpaste-storage-keyring.h>
#endif

#ifdef G_PASTE_ENABLE_ENCRYPTION
#define GCR_API_SUBJECT_TO_CHANGE
#include <gcr/gcr.h>

/* The daemon's single master passphrase for the encrypted backend, obtained
 * once at startup (prompt or keyring). Kept process-wide in gcr secure memory
 * so every history the daemon builds resolves the encrypted backend without
 * threading the secret through every constructor. */
static gchar *g_paste_storage_passphrase = NULL;

/* Copy a passphrase into gcr secure (non-pageable) memory. NULL for a NULL or
 * empty one; free it with g_paste_storage_passphrase_free(). */
static gchar *
g_paste_storage_passphrase_dup (const gchar *passphrase)
{
    return (passphrase && *passphrase) ? gcr_secure_memory_strdup (passphrase) : NULL;
}

/* Wipe and release a passphrase copy. */
static void
g_paste_storage_passphrase_free (gchar *passphrase)
{
    gcr_secure_memory_strfree (passphrase);
}

/**
 * g_paste_storage_backend_set_passphrase:
 * @passphrase: (nullable): the master passphrase, or %NULL to clear it
 *
 * Set the passphrase used by every encrypted file backend created afterwards.
 */
G_PASTE_VISIBLE void
g_paste_storage_backend_set_passphrase (const gchar *passphrase)
{
    g_paste_storage_passphrase_free (g_paste_storage_passphrase);
    g_paste_storage_passphrase = g_paste_storage_passphrase_dup (passphrase);
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

/* Process-wide advisory lock serialising history persistence across daemons.
 * Whoever persists holds it for its whole lifetime; a daemon taking over (the
 * gnome-shell extension replacing the standalone daemon, or vice versa) blocks
 * on it before its first read, so it never loads a stale history while the
 * previous owner is still flushing its final state. The kernel drops the lock
 * when the holder's process dies, so a crash cannot wedge the successor. */
static GMutex   g_paste_storage_lock_mutex;
static gint     g_paste_storage_lock_fd = -1;
/* Set by _unlock() so a wait still in progress on the worker thread gives up
 * instead of installing a lock nobody wants any more. Written under the mutex,
 * which is only ever held for the bookkeeping — never across the wait — hence
 * read atomically from the waiting thread. */
static gint     g_paste_storage_lock_released;

/**
 * g_paste_storage_backend_lock:
 *
 * Acquire the process-wide history lock, blocking until any other daemon holding
 * it releases it (or its process dies). Idempotent: once held, further calls are
 * a no-op. Meant to be called from the history-loading worker thread, before the
 * first read, so a takeover waits out the previous owner's final write.
 */
G_PASTE_VISIBLE void
g_paste_storage_backend_lock (void)
{
#ifdef G_OS_UNIX
    {
        g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&g_paste_storage_lock_mutex);

        if (g_paste_storage_lock_fd >= 0)
            return;

        g_atomic_int_set (&g_paste_storage_lock_released, FALSE);
    }

    g_autofree gchar *dir = g_paste_util_get_history_dir_path ();

    g_mkdir_with_parents (dir, 0700);

    g_autofree gchar *path = g_build_filename (dir, "lock", NULL);
    gint fd = open (path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);

    if (fd < 0)
    {
        g_warning ("Failed to open the history lock file: %s", g_strerror (errno));
        return;
    }

    gint64 start = g_get_monotonic_time ();
    gboolean logged = FALSE;

    /* The wait deliberately happens *outside* the mutex: it can last as long as
     * the previous daemon takes to finish writing, and _unlock() runs on the main
     * thread of every exit path — holding the mutex here would block it (and, in
     * the gnome-shell host, freeze the compositor) for that whole time. */
    while (flock (fd, LOCK_EX | LOCK_NB) < 0)
    {
        if (errno != EWOULDBLOCK && errno != EINTR)
        {
            g_warning ("Failed to lock the history: %s", g_strerror (errno));
            close (fd);
            return;
        }

        /* A brief handover is normal; only warn if the previous daemon really is
         * taking a long time to finish writing, so a stuck one is diagnosable. */
        if (!logged && g_get_monotonic_time () - start > 5 * G_USEC_PER_SEC)
        {
            g_message ("Waiting for the previous GPaste daemon to finish writing the history");
            logged = TRUE;
        }

        if (g_atomic_int_get (&g_paste_storage_lock_released))
        {
            close (fd);
            return;
        }

        g_usleep (100 * 1000); /* 100ms */
    }

    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&g_paste_storage_lock_mutex);

    /* Someone unlocked (or won the race) while we were waiting: drop ours. */
    if (g_paste_storage_lock_fd >= 0 || g_atomic_int_get (&g_paste_storage_lock_released))
        close (fd);
    else
        g_paste_storage_lock_fd = fd;
#endif
}

/**
 * g_paste_storage_backend_unlock:
 *
 * Release the process-wide history lock acquired with
 * g_paste_storage_backend_lock(). Call it after the final write has been flushed,
 * so a daemon taking over can proceed. Safe to call when the lock is not held.
 */
G_PASTE_VISIBLE void
g_paste_storage_backend_unlock (void)
{
#ifdef G_OS_UNIX
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&g_paste_storage_lock_mutex);

    g_atomic_int_set (&g_paste_storage_lock_released, TRUE);

    if (g_paste_storage_lock_fd < 0)
        return;

    /* Closing the fd drops the flock. */
    close (g_paste_storage_lock_fd);
    g_paste_storage_lock_fd = -1;
#endif
}

typedef struct
{
    GPasteSettings *settings;
} GPasteStorageBackendPrivate;

G_PASTE_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (StorageBackend, storage_backend, G_TYPE_OBJECT)

static gchar *
_g_paste_storage_backend_get_history_file_path (GPasteStorageBackend *self,
                                                const gchar          *name)
{
    return g_paste_util_get_history_file_path (name, G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->get_extension (self));
}

/**
 * g_paste_storage_backend_read_history:
 * @self: a #GPasteItem instance
 * @name: the name of the history to load
 * @history: (out) (element-type GPasteItem): the history we just read
 * @size: (out): the size used by the history
 *
 * Reads the history from our storage backend
 *
 * Returns: %FALSE when the history exists on disk but could not be read back
 *          (a failed decryption, parse or I/O error), %TRUE otherwise (including
 *          a genuinely empty or absent history)
 */
G_PASTE_VISIBLE gboolean
g_paste_storage_backend_read_history (GPasteStorageBackend  *self,
                                      const gchar           *name,
                                      GList                **history,
                                      gsize                 *size)
{
    g_return_val_if_fail (G_PASTE_IS_STORAGE_BACKEND (self), FALSE);
    g_return_val_if_fail (name, FALSE);
    g_return_val_if_fail (history && !*history, FALSE);
    g_return_val_if_fail (size, FALSE);

    /* @history is required to come in empty (asserted above), but @size is not:
     * settle it here so every backend's failure path — which reports the failure
     * rather than filling these in — leaves both (out) parameters consistent
     * with each other and with the empty history the caller is holding. */
    *size = 0;

    g_autofree gchar *history_file_path = _g_paste_storage_backend_get_history_file_path (self, name);

    return G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->read_history_file (self, history_file_path, history, size);
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
g_paste_storage_backend_write_history (GPasteStorageBackend *self,
                                       const gchar          *name,
                                       const GList          *history)
{
    g_return_if_fail (G_PASTE_IS_STORAGE_BACKEND (self));
    g_return_if_fail (name);

    g_autofree gchar *history_file_path = _g_paste_storage_backend_get_history_file_path (self, name);

    G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->write_history_file (self, history_file_path, history);
}

/* Whether any storage flavor still keeps a history under @name on disk. The
 * images/<name>/ directory belongs to the *name*, not to one backend: after a
 * migration the source's delete_history runs while the destination still
 * references (or just materialized) images in that very directory, so it must
 * only go away once no flavor stores the name anymore. */
static gboolean
_g_paste_storage_backend_history_still_stored (const gchar *name)
{
    /* Check every storage flavor's on-disk file. Each kind's extension comes
     * from the shared g_paste_storage_get_extension(), so a new flavor is
     * covered here automatically (the non-storing NOOP's extension simply never
     * matches a real file). */
    for (GPasteStorage kind = 0; kind < G_PASTE_N_STORAGE; ++kind)
    {
        g_autoptr (GFile) file = g_paste_util_get_history_file (name, g_paste_storage_get_extension (kind));

        if (g_file_query_exists (file,
                                 NULL)) /* cancellable */
            return TRUE;
    }

    return FALSE;
}

/* A history's images live in their own directory (so histories never share,
 * nor cross-delete, image files): deleting the history deletes them with it,
 * whichever backend stored the history itself. */
static void
_g_paste_storage_backend_delete_history_images (const gchar *name)
{
    g_autofree gchar *images_dir_path = g_paste_image_item_get_images_dir (name);
    g_autoptr (GFile) images_dir = g_file_new_for_path (images_dir_path);
    g_autoptr (GFileEnumerator) children = g_file_enumerate_children (images_dir,
                                                                      G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                                      G_FILE_QUERY_INFO_NONE,
                                                                      NULL, NULL);

    if (children)
    {
        GFileInfo *info;

        while ((info = g_file_enumerator_next_file (children, NULL, NULL)))
        {
            g_autoptr (GFileInfo) child_info = info;
            g_autoptr (GFile) child = g_file_enumerator_get_child (children, child_info);
            g_autoptr (GError) error = NULL;

            if (!g_file_delete (child, NULL, &error))
                g_warning ("Failed to delete image file: %s", error->message);
        }
    }

    g_autoptr (GError) error = NULL;

    if (!g_file_delete (images_dir, NULL, &error) &&
        !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        g_warning ("Failed to delete images directory: %s", error->message);
}

/**
 * g_paste_storage_backend_delete_history:
 * @self: a #GPasteStorageBackend instance
 * @name: the name of the history to delete
 * @error: return location for a #GError, or %NULL
 *
 * Delete a history from our storage backend
 *
 * Backends only ever propagate what unlinking their store returned, so errors
 * land in %G_IO_ERROR or %G_FILE_ERROR. The image sweep that follows is
 * best-effort and never reported here.
 */
G_PASTE_VISIBLE void
g_paste_storage_backend_delete_history (GPasteStorageBackend  *self,
                                         const gchar          *name,
                                         GError              **error)
{
    g_return_if_fail (G_PASTE_IS_STORAGE_BACKEND (self));
    g_return_if_fail (name);

    if (G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->delete_history)
        G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->delete_history (self, name, error);

    /* Only sweep the images once the name is gone from every flavor: another
     * backend may still reference them (a migration's destination, whose
     * cleanup deletes the source history under the same name). */
    if (!_g_paste_storage_backend_history_still_stored (name))
        _g_paste_storage_backend_delete_history_images (name);
}

/* The default list_histories: enumerate the history dir for files of this
 * backend's flavour (its get_extension suffix), so e.g. plain ".xml" and
 * encrypted ".xmls" histories never get mixed up. */
static GStrv
_g_paste_storage_backend_list_histories_by_extension (GPasteStorageBackend  *self,
                                                      GError               **error)
{
    g_autoptr (GStrvBuilder) history_names = g_strv_builder_new ();
    g_autoptr (GFile) history_dir = g_paste_util_get_history_dir ();
    g_autofree gchar *suffix = g_strconcat (".", G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->get_extension (self), NULL);
    gsize suffix_len = strlen (suffix);
    g_autoptr (GFileEnumerator) histories = g_file_enumerate_children (history_dir,
                                                                       G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                                                                       G_FILE_QUERY_INFO_NONE,
                                                                       NULL,
                                                                       error);
    /* A missing history dir (fresh profile) is not an error: return an empty
     * list. Check the enumerator itself, since callers may pass error == NULL. */
    if (!histories)
    {
        if (error && *error)
        {
            if ((*error)->domain == G_IO_ERROR && (*error)->code == G_IO_ERROR_NOT_FOUND)
                g_clear_error (error);
            else
                return NULL;
        }
        return g_strv_builder_end (history_names);
    }

    GFileInfo *history;
    g_autoptr (GError) local_error = NULL;

    while ((history = g_file_enumerator_next_file (histories,
                                                   NULL,
                                                   &local_error)))
    {
        g_autoptr (GFileInfo) h = history;
        const gchar *raw_name = g_file_info_get_display_name (h);

        if (g_str_has_suffix (raw_name, suffix))
        {
            g_autofree gchar *name = g_strdup (raw_name);

            name[strlen (name) - suffix_len] = '\0';
            g_strv_builder_take (history_names, g_steal_pointer (&name));
        }
    }

    /* next_file() returns NULL both at the end of the listing and on a failure,
     * so the error can only be checked once the loop is over. A truncated listing
     * must never look like a successful one: a migration would then import (and
     * its cleanup delete) only the histories we happened to reach. */
    if (local_error)
    {
        g_propagate_error (error, g_steal_pointer (&local_error));
        return NULL;
    }

    return g_strv_builder_end (history_names);
}

/**
 * g_paste_storage_backend_list_histories:
 * @self: a #GPasteStorageBackend instance
 * @error: return location for a #GError, or %NULL
 *
 * Get the list of available histories from our storage backend
 *
 * Errors come from enumerating the store, in %G_IO_ERROR or %G_FILE_ERROR. A
 * partial listing is never returned as a success: if any flavor fails, so does
 * the call.
 *
 * Returns: (transfer full): The list of history names
 */
G_PASTE_VISIBLE GStrv
g_paste_storage_backend_list_histories (GPasteStorageBackend  *self,
                                         GError              **error)
{
    g_return_val_if_fail (G_PASTE_IS_STORAGE_BACKEND (self), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    if (G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->list_histories)
        return G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->list_histories (self, error);

    return _g_paste_storage_backend_list_histories_by_extension (self, error);
}

/**
 * g_paste_storage_backend_rekey:
 * @self: a #GPasteStorageBackend instance, holding the passphrase @name is
 *        currently encrypted with
 * @name: the name of the history to re-encrypt
 * @new_passphrase: the passphrase to encrypt @name with from now on
 *
 * Re-encrypt an existing history under @new_passphrase, in place: the data stays
 * where it is, only the key changes. Meant for a passphrase change, which —
 * unlike a migration — has no second backend to copy into.
 *
 * Only the encrypted flavors can do this, and each does it its own way (a
 * database re-encrypts its columns in one transaction, a file rewrites itself
 * and its image side files), so this dispatches to the backend rather than
 * trying to be generic. A backend that cannot re-key says so instead of
 * reporting a success that never happened.
 *
 * Returns: %FALSE when @name was left exactly as it was, so a caller re-keying
 *          several histories can stop rather than split them across two
 *          passphrases
 */
G_PASTE_VISIBLE gboolean
g_paste_storage_backend_rekey (GPasteStorageBackend *self,
                               const gchar          *name,
                               const gchar          *new_passphrase)
{
    g_return_val_if_fail (G_PASTE_IS_STORAGE_BACKEND (self), FALSE);
    g_return_val_if_fail (name, FALSE);
    g_return_val_if_fail (new_passphrase && *new_passphrase, FALSE);

    if (!G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->rekey)
    {
        g_warning ("This storage backend has no passphrase to change");
        return FALSE;
    }

    return G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->rekey (self, name, new_passphrase);
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
g_paste_storage_backend_add_item (GPasteStorageBackend *self,
                                  const gchar          *name,
                                  GPasteItem           *item,
                                  const GList          *history)
{
    g_return_if_fail (G_PASTE_IS_STORAGE_BACKEND (self));
    g_return_if_fail (name);

    if (G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->add_item)
        G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->add_item (self, name, item, history);
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
g_paste_storage_backend_remove_item (GPasteStorageBackend *self,
                                     const gchar          *name,
                                     const gchar          *uuid,
                                     const GList          *history)
{
    g_return_if_fail (G_PASTE_IS_STORAGE_BACKEND (self));
    g_return_if_fail (name);

    if (G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->remove_item)
        G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->remove_item (self, name, uuid);
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
g_paste_storage_backend_replace_item (GPasteStorageBackend *self,
                                      const gchar          *name,
                                      const gchar          *old_uuid,
                                      GPasteItem           *item,
                                      const GList          *history)
{
    g_return_if_fail (G_PASTE_IS_STORAGE_BACKEND (self));
    g_return_if_fail (name);
    g_return_if_fail (old_uuid);

    if (G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->replace_item)
        G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->replace_item (self, name, old_uuid, item);
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
g_paste_storage_backend_clear_history (GPasteStorageBackend *self,
                                       const gchar          *name,
                                       const GList          *history)
{
    g_return_if_fail (G_PASTE_IS_STORAGE_BACKEND (self));
    g_return_if_fail (name);

    if (G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->clear_history)
        G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->clear_history (self, name);
    else
        g_paste_storage_backend_write_history (self, name, history);
}

/**
 * g_paste_storage_backend_is_incremental:
 * @self: a #GPasteStorageBackend instance
 *
 * Whether the backend implements all of the incremental update methods, so it
 * never needs a full snapshot outside of an add (whose snapshot it reconciles
 * against) and never takes the full-rewrite fallback. A backend implementing
 * only some of them counts as non-incremental: the add/remove/replace/clear
 * wrappers then fall back to rewriting the whole history, and callers may
 * coalesce successive updates into a single write.
 *
 * Returns: %TRUE if every incremental update method is implemented
 */
G_PASTE_VISIBLE gboolean
g_paste_storage_backend_is_incremental (GPasteStorageBackend *self)
{
    g_return_val_if_fail (G_PASTE_IS_STORAGE_BACKEND (self), FALSE);

    const GPasteStorageBackendClass *klass = G_PASTE_STORAGE_BACKEND_GET_CLASS (self);

    return klass->add_item && klass->remove_item && klass->replace_item && klass->clear_history;
}

static void
g_paste_storage_backend_dispose (GObject *object)
{
    GPasteStorageBackendPrivate *priv = g_paste_storage_backend_get_instance_private (G_PASTE_STORAGE_BACKEND (object));

    g_clear_object (&priv->settings);

    G_OBJECT_CLASS (g_paste_storage_backend_parent_class)->dispose (object);
}

static GPasteSettings *
g_paste_storage_backend_get_settings (GPasteStorageBackend *self)
{
    const GPasteStorageBackendPrivate *priv = g_paste_storage_backend_get_instance_private (self);

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
    klass->rekey = NULL;

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
#ifdef G_PASTE_ENABLE_SQLITE
    case G_PASTE_STORAGE_SQLITE:
        return G_PASTE_TYPE_SQLITE_BACKEND;
#endif
    case G_PASTE_STORAGE_ENCRYPTED_FILE:
    case G_PASTE_STORAGE_ENCRYPTED_SQLITE:
        /* Unreachable in practice: g_paste_storage_backend_new () constructs
         * the encrypted flavor (or downgrades to NOOP) before ever calling us.
         * Keep the history in memory rather than silently writing
         * would-be-encrypted data as plaintext, whatever the caller. */
        return G_PASTE_TYPE_NOOP_BACKEND;
    default:
        /* G_PASTE_STORAGE_DEFAULT, and any unexpected value, map to the plain
         * file backend; return it directly rather than recursing. */
        return G_PASTE_TYPE_FILE_BACKEND;
    }
}

/**
 * g_paste_storage_get_extension:
 * @storage_kind: a #GPasteStorage kind
 *
 * The on-disk file extension a given storage kind uses. This is the single
 * source of truth every backend's get_extension vtable, the migration
 * detection and the shared images cleanup derive from, so a new flavour only
 * needs its extension declared here.
 *
 * Returns: the extension (without the leading dot)
 */
G_PASTE_VISIBLE const gchar *
g_paste_storage_get_extension (GPasteStorage storage_kind)
{
    switch (storage_kind)
    {
    case G_PASTE_STORAGE_FILE:
        return "xml";
    case G_PASTE_STORAGE_ENCRYPTED_FILE:
        return "xmls";
    case G_PASTE_STORAGE_SQLITE:
        return "db";
    case G_PASTE_STORAGE_ENCRYPTED_SQLITE:
        return "dbs";
    case G_PASTE_STORAGE_NOOP:
    default:
        /* The no-storage backend keeps nothing on disk; its extension only ever
         * feeds a listing that never matches anything. */
        return "noop";
    }
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/**
 * g_paste_storage_passphrase_can_decrypt:
 * @storage_kind: the encrypted #GPasteStorage kind to verify against
 * @settings: a #GPasteSettings instance
 * @passphrase: the passphrase to check
 *
 * Check whether @passphrase actually unlocks the existing encrypted history of
 * the @storage_kind flavor, so a wrong (or stale keyring) passphrase is never
 * accepted and given the chance to overwrite the real data.
 *
 * Returns: %FALSE only when encrypted data is present and @passphrase does not
 *          unlock it
 */
G_PASTE_VISIBLE gboolean
g_paste_storage_passphrase_can_decrypt (GPasteStorage   storage_kind,
                                        GPasteSettings *settings,
                                        const gchar    *passphrase)
{
    switch (storage_kind)
    {
#ifdef G_PASTE_ENABLE_SQLITE
    case G_PASTE_STORAGE_ENCRYPTED_SQLITE:
        return g_paste_sqlite_backend_passphrase_can_decrypt (settings, passphrase);
#endif
    default:
        return g_paste_file_backend_passphrase_can_decrypt (settings, passphrase);
    }
}

/* NULL when this build cannot construct the requested encrypted flavor. */
static GPasteStorageBackend *
_g_paste_storage_backend_new_encrypted (GPasteStorage   storage_kind,
                                        GPasteSettings *settings,
                                        const gchar    *passphrase)
{
    switch (storage_kind)
    {
    case G_PASTE_STORAGE_ENCRYPTED_FILE:
        return g_paste_file_backend_new_encrypted (settings, passphrase);
#ifdef G_PASTE_ENABLE_SQLITE
    case G_PASTE_STORAGE_ENCRYPTED_SQLITE:
        return g_paste_sqlite_backend_new_encrypted (settings, passphrase);
#endif
    default:
        return NULL;
    }
}
#endif

/* The shared constructor: @passphrase is the key an encrypted @storage_kind is
 * built with, already resolved by the caller (%NULL when there is none). An
 * encrypted kind we cannot key — or cannot build in this configuration — always
 * degrades to "no storage", never to plaintext on disk. */
static GPasteStorageBackend *
_g_paste_storage_backend_new (GPasteStorage   storage_kind,
                              GPasteSettings *settings,
                              const gchar    *passphrase G_GNUC_UNUSED)
{
    if (g_paste_storage_is_encrypted (storage_kind))
    {
#ifdef G_PASTE_ENABLE_ENCRYPTION
        if (passphrase)
        {
            GPasteStorageBackend *backend = _g_paste_storage_backend_new_encrypted (storage_kind, settings, passphrase);

            if (backend)
                return backend;
        }

        /* Without a passphrase (or the flavor's support built in) we must not
         * fall back to plaintext on disk; keep the history in memory only. */
        g_warning ("No passphrase for the encrypted storage backend; not storing the history");
#else
        g_warning ("Encrypted storage is not built in; not storing the history");
#endif
        storage_kind = G_PASTE_STORAGE_NOOP;
    }

    GPasteStorageBackend *self = g_object_new (_g_paste_storage_backend_get_type (storage_kind), NULL);
    GPasteStorageBackendPrivate *priv = g_paste_storage_backend_get_instance_private (self);

    priv->settings = g_object_ref (settings);

    return self;
}

/**
 * g_paste_storage_backend_new:
 * @storage_kind: the kind of storage we want to use to save and load history
 * @settings: a #GPasteSettings instance
 *
 * Create a new instance of #GPasteStorageBackend, keyed (when @storage_kind is
 * encrypted) with the process-wide passphrase, falling back to the one
 * remembered in the keyring.
 *
 * Returns: a newly allocated #GPasteStorageBackend
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteStorageBackend *
g_paste_storage_backend_new (GPasteStorage   storage_kind,
                             GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    const gchar *passphrase = NULL;

#ifdef G_PASTE_ENABLE_ENCRYPTION
    passphrase = g_paste_storage_backend_get_passphrase ();

#ifdef G_PASTE_ENABLE_LIBSECRET
    /* No passphrase set in this process yet (e.g. the in-process daemon in
     * gnome-shell never ran the prompt): fall back to the one remembered in
     * the keyring before giving up. apply_verified only keeps it when it
     * actually decrypts the history, so a stale entry can never be used (which
     * would overwrite the real data with an empty, wrongly-encrypted one). */
    if (!passphrase && g_paste_storage_is_encrypted (storage_kind) &&
        g_paste_storage_keyring_apply_verified (storage_kind, settings))
        passphrase = g_paste_storage_backend_get_passphrase ();
#endif
#endif

    return _g_paste_storage_backend_new (storage_kind, settings, passphrase);
}

/**
 * g_paste_storage_backend_new_with_passphrase:
 * @storage_kind: the kind of storage we want to use to save and load history
 * @settings: a #GPasteSettings instance
 * @passphrase: (nullable): the passphrase to key an encrypted @storage_kind with
 *
 * Create a new instance of #GPasteStorageBackend keyed with exactly
 * @passphrase, deliberately without the process-wide / keyring fallback
 * g_paste_storage_backend_new() applies: a migration between two encrypted
 * flavors has to hold the source and the destination open under two different
 * keys at once, and the source must never end up opened with the destination's
 * one (which reads back empty and would pass for an emptied history). Ignored
 * for the plain kinds, which have nothing to key.
 *
 * Returns: a newly allocated #GPasteStorageBackend
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteStorageBackend *
g_paste_storage_backend_new_with_passphrase (GPasteStorage   storage_kind,
                                             GPasteSettings *settings,
                                             const gchar    *passphrase)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    return _g_paste_storage_backend_new (storage_kind, settings, passphrase);
}
