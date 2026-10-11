// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-util.h>

#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-file-backend.h>
#include <gpaste-daemon/gpaste-noop-backend.h>
#include <gpaste-daemon/gpaste-prompt.h>

#include <glib/gstdio.h>

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
#include <gpaste-daemon/gpaste-passphrase.h>
#include <gpaste-daemon/gpaste-secret-stream-converter.h>

/* The daemon's single master passphrase for the encrypted backend, obtained
 * once at startup (prompt or keyring). Kept process-wide in gcr secure memory
 * so every history the daemon builds resolves the encrypted backend without
 * threading the secret through every constructor. */
static GPastePassphrase *g_paste_storage_passphrase = NULL;

/**
 * g_paste_storage_backend_set_passphrase:
 * @passphrase: (nullable): the master passphrase, or %NULL to clear it
 *
 * Set the passphrase used by every encrypted file backend created afterwards.
 */
G_PASTE_VISIBLE void
g_paste_storage_backend_set_passphrase (const gchar *passphrase)
{
    g_paste_passphrase_free (g_paste_storage_passphrase);
    g_paste_storage_passphrase = g_paste_passphrase_new (passphrase);
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
    return g_paste_passphrase_peek (g_paste_storage_passphrase);
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

#ifdef G_OS_UNIX
/* The one file both lock () and try_lock () take, made where need be: -1 when
 * it cannot be opened, with a warning saying why. A history directory that
 * cannot be made is the helper's own to report: once it has logged, errno
 * says nothing of it.
 *
 * A lock file that cannot be opened or locked tells no daemon apart, so both
 * answer that the store may be touched: refused instead, every load would read
 * nothing, the history it was for staying unwritten, and every Backup, Empty or
 * Delete would blame another daemon, for as long as the file stays as it is. */
static gint
g_paste_storage_backend_open_lock_file (void)
{
    if (!g_paste_util_ensure_history_dir_exists ())
        return -1;

    g_autofree gchar *dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *path = g_build_filename (dir, "lock", NULL);
    gint fd = open (path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);

    if (fd < 0)
        g_warning ("Failed to open the history lock file: %s", g_strerror (errno));

    return fd;
}
#endif

/**
 * g_paste_storage_backend_lock:
 * @cancellable: (nullable): the load this wait belongs to, to give it up with
 *
 * Acquire the process-wide history lock, blocking until any other daemon holding
 * it releases it (or its process dies). Idempotent: once held, further calls are
 * a no-op. Meant to be called from the history-loading worker thread, before the
 * first read, so a takeover waits out the previous owner's final write.
 *
 * Two things end the wait short of the lock, and they answer different
 * questions: @cancellable says the load this is waiting for has been given up on
 * (superseded, or abandoned by the owner), where the released flag says this
 * process has let go of the lock and wants none. Either way no lock is
 * installed.
 */
G_PASTE_VISIBLE void
g_paste_storage_backend_lock (GCancellable *cancellable)
{
#ifdef G_OS_UNIX
    {
        g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&g_paste_storage_lock_mutex);

        if (g_paste_storage_lock_fd >= 0)
            return;

        g_atomic_int_set (&g_paste_storage_lock_released, FALSE);
    }

    gint fd = g_paste_storage_backend_open_lock_file ();

    /* open_lock_file () says why it failed, and why that refuses nothing. */
    if (fd < 0)
        return;

    gint64 start = g_get_monotonic_time ();
    gboolean logged = FALSE;

    /* The wait deliberately happens *outside* the mutex: it can last as long as
     * the previous daemon takes to finish writing, and _unlock() runs on the main
     * thread of every exit path — holding the mutex here would block it (and, in
     * the gnome-shell host, freeze the compositor) for that whole time.
     *
     * Each attempt is made under it, though, and the lock installed the moment
     * it is taken: try_lock () holds the mutex too, and finding this file
     * holding the lock not yet installed, its own flock () would be refused as
     * if another daemon held it. */
    for (;;)
    {
        gint refused;

        {
            g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&g_paste_storage_lock_mutex);

            /* Another wait of this process's got it, and a lock of this
             * process's is one flock () refuses this wait's own file. */
            if (g_paste_storage_lock_fd >= 0)
            {
                close (fd);
                return;
            }

            if (flock (fd, LOCK_EX | LOCK_NB) == 0)
            {
                /* A process that has let go of the lock wants none. */
                if (g_atomic_int_get (&g_paste_storage_lock_released))
                    close (fd);
                else
                    g_paste_storage_lock_fd = fd;
                return;
            }

            refused = errno;
        }

        if (refused != EWOULDBLOCK && refused != EINTR)
        {
            g_warning ("Failed to lock the history: %s", g_strerror (refused));
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

        if (g_atomic_int_get (&g_paste_storage_lock_released) ||
            g_cancellable_is_cancelled (cancellable))
        {
            close (fd);
            return;
        }

        g_usleep (100 * 1000); /* 100ms */
    }
#endif
}

/**
 * g_paste_storage_backend_try_lock:
 *
 * Acquire the process-wide history lock if nobody else holds it, without
 * waiting: for what may only touch the store while no other daemon can be
 * writing it, and has a later chance if one is (see
 * g_paste_file_backend_settle_rekeys()). Like g_paste_storage_backend_lock(),
 * once held it stays held.
 *
 * Returns: whether this process may touch the store: it holds the lock, or the
 *     lock file cannot be opened or locked, which no daemon can then hold either
 */
G_PASTE_VISIBLE gboolean
g_paste_storage_backend_try_lock (void)
{
#ifdef G_OS_UNIX
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&g_paste_storage_lock_mutex);

    if (g_paste_storage_lock_fd >= 0)
        return TRUE;

    gint fd = g_paste_storage_backend_open_lock_file ();

    if (fd < 0)
        return TRUE;

    if (flock (fd, LOCK_EX | LOCK_NB) < 0)
    {
        gboolean held_elsewhere = (errno == EWOULDBLOCK || errno == EINTR);

        if (!held_elsewhere)
            g_warning ("Failed to lock the history: %s", g_strerror (errno));
        close (fd);
        return !held_elsewhere;
    }

    g_atomic_int_set (&g_paste_storage_lock_released, FALSE);
    g_paste_storage_lock_fd = fd;
#endif

    return TRUE;
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

/* A backend that can update the stored history in place does; one that cannot
 * has the whole thing written out again. */
#define G_PASTE_STORAGE_BACKEND_UPDATE(vfunc, ...)                                         \
    do {                                                                                   \
        const GPasteStorageBackendClass *klass = G_PASTE_STORAGE_BACKEND_GET_CLASS (self); \
                                                                                           \
        if (klass->vfunc)                                                                  \
            klass->vfunc (self, name, ##__VA_ARGS__);                                      \
        else                                                                               \
            g_paste_storage_backend_write_history (self, name, history);                   \
    } while (FALSE)

/**
 * g_paste_storage_backend_get_kind:
 * @self: a #GPasteStorageBackend instance
 *
 * Which storage flavour @self is.
 *
 * Returns: the #GPasteStorage kind
 */
G_PASTE_VISIBLE GPasteStorage
g_paste_storage_backend_get_kind (GPasteStorageBackend *self)
{
    g_return_val_if_fail (G_PASTE_IS_STORAGE_BACKEND (self), G_PASTE_STORAGE_NOOP);

    const GPasteStorageBackendClass *klass = G_PASTE_STORAGE_BACKEND_GET_CLASS (self);

    g_return_val_if_fail (klass->get_kind, G_PASTE_STORAGE_NOOP);

    return klass->get_kind (self);
}

/**
 * g_paste_storage_backend_get_extension:
 * @self: a #GPasteStorageBackend instance
 *
 * The on-disk extension @self stores histories under.
 *
 * Returns: the extension (without the leading dot)
 */
G_PASTE_VISIBLE const gchar *
g_paste_storage_backend_get_extension (GPasteStorageBackend *self)
{
    return g_paste_storage_get_extension (g_paste_storage_backend_get_kind (self));
}

/**
 * g_paste_storage_backend_is_encrypted:
 * @self: a #GPasteStorageBackend instance
 *
 * Whether @self encrypts the history on disk -- which is also what decides
 * whether it may keep password items at all.
 *
 * Returns: %TRUE for the encrypted flavours
 */
G_PASTE_VISIBLE gboolean
g_paste_storage_backend_is_encrypted (GPasteStorageBackend *self)
{
    return g_paste_storage_is_encrypted (g_paste_storage_backend_get_kind (self));
}

/**
 * g_paste_storage_backend_get_history_file_path:
 * @self: a #GPasteStorageBackend instance
 * @name: the name of a history
 *
 * Where this backend keeps the history called @name. A history is identified by
 * its name everywhere; the path is each backend's own business, which is why it
 * derives it here rather than being handed one.
 *
 * Returns: the newly allocated path
 */
G_PASTE_VISIBLE gchar *
g_paste_storage_backend_get_history_file_path (GPasteStorageBackend *self,
                                               const gchar          *name)
{
    g_return_val_if_fail (G_PASTE_IS_STORAGE_BACKEND (self), NULL);
    g_return_val_if_fail (name, NULL);

    return g_paste_util_get_history_file_path (name, g_paste_storage_backend_get_extension (self));
}

/**
 * g_paste_storage_backend_read_history:
 * @self: a #GPasteItem instance
 * @name: the name of the history to load
 * @cancellable: (nullable): a #GCancellable to give the read up on
 * @history: (out) (element-type GPasteItem): the history we just read
 * @size: (out): the size used by the history
 *
 * Reads the history from our storage backend
 *
 * Returns: %FALSE when the history exists on disk but could not be read back
 *          (a failed decryption, parse or I/O error, or @cancellable giving up
 *          on it), %TRUE otherwise (including a genuinely empty or absent
 *          history)
 */
G_PASTE_VISIBLE gboolean
g_paste_storage_backend_read_history (GPasteStorageBackend *self,
                                      const gchar          *name,
                                      GCancellable         *cancellable,
                                      GList               **history,
                                      gsize                *size)
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

    return G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->read_history_file (self, name, cancellable, history, size);
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

    G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->write_history_file (self, name, history);
}

/* Whether a file flavour still keeps a history under @name. The images/<name>/
 * directory belongs to the *name*, not to one backend: after a migration the
 * source's delete_history runs while the destination still references (or just
 * materialized) images in that very directory, so it must only go away once no
 * flavour reading it stores the name anymore. The database flavours keep their
 * images in the store and read no file there, so they do not count. */
static gboolean
_g_paste_storage_backend_history_still_stored (const gchar *name)
{
    static const GPasteStorage file_flavours[] = { G_PASTE_STORAGE_FILE, G_PASTE_STORAGE_ENCRYPTED_FILE };

    for (gsize i = 0; i < G_N_ELEMENTS (file_flavours); ++i)
    {
        if (g_paste_util_history_is_stored (name, file_flavours[i]))
            return TRUE;
    }

    return FALSE;
}

/**
 * g_paste_storage_backend_delete_history_images:
 * @name: the name of the history whose images to sweep
 * @error: return location for a #GError, or %NULL
 *
 * A history's images live in their own directory (so histories never share,
 * nor cross-delete, image files): deleting or emptying the history takes them
 * with it, whichever backend stored the history itself.
 *
 * Keyed by name alone, no backend instance being needed to find them, so a
 * caller holding only the name of a history that is not the current one can
 * sweep it too.
 *
 * Returns: %FALSE, with @error set to the first thing that went wrong, when
 *          anything the sweep should have removed is still on disk. The rest is
 *          swept anyway: these are the clipboard's screenshots, so every one
 *          that can go, goes.
 */
G_PASTE_VISIBLE gboolean
g_paste_storage_backend_delete_history_images (const gchar *name,
                                               GError     **error)
{
    g_autofree gchar *images_dir_path = g_paste_file_backend_images_dir (name);
    g_autoptr (GFile) images_dir = g_file_new_for_path (images_dir_path);
    g_autoptr (GError) failure = NULL;
    /* A history that never held an image has no directory to sweep, and comes
     * back as an empty list. A listing that failed is not that: sweeping what it
     * did return would leave the rest behind, so there is nothing to do here but
     * say so. */
    g_auto (GStrv) images = g_paste_util_list_directory (images_dir, G_FILE_ATTRIBUTE_STANDARD_NAME, &failure);

    if (!images)
    {
        g_propagate_prefixed_error (error, g_steal_pointer (&failure),
                                    "Could not list the images of \"%s\": ", name);

        return FALSE;
    }

    for (GStrv image = images; *image; ++image)
    {
        g_autoptr (GFile) child = g_file_get_child (images_dir, *image);
        g_autoptr (GError) delete_error = NULL;

        if (!g_file_delete (child, NULL, &delete_error) && !failure)
            failure = g_steal_pointer (&delete_error);
    }

    g_autoptr (GError) rmdir_error = NULL;

    /* Whatever the loop left behind makes this fail too, with a less specific
     * message: the first failure is the one that says why. */
    if (!g_file_delete (images_dir, NULL, &rmdir_error) &&
        !g_error_matches (rmdir_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) &&
        !failure)
        failure = g_steal_pointer (&rmdir_error);

    if (!failure)
        return TRUE;

    g_propagate_error (error, g_steal_pointer (&failure));

    return FALSE;
}

/**
 * g_paste_storage_backend_delete_history:
 * @self: a #GPasteStorageBackend instance
 * @name: the name of the history to delete
 * @error: return location for a #GError, or %NULL
 *
 * Delete a history from our storage backend
 *
 * Backends propagate what unlinking their store returned, and the image sweep
 * that follows propagates its own, so errors land in %G_IO_ERROR or
 * %G_FILE_ERROR. The sweep is reported because those files are the clipboard's
 * screenshots in the plain flavours: "the history is gone" must not be answered
 * while they are still there.
 */
G_PASTE_VISIBLE void
g_paste_storage_backend_delete_history (GPasteStorageBackend *self,
                                        const gchar          *name,
                                        GError              **error)
{
    g_return_if_fail (G_PASTE_IS_STORAGE_BACKEND (self));
    g_return_if_fail (name);

    if (G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->delete_history)
        G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->delete_history (self, name, error);

    /* Only sweep the images once the name is gone from both file flavours:
     * the other may still reference them (a migration's destination, whose
     * cleanup deletes the source history under the same name). */
    if (_g_paste_storage_backend_history_still_stored (name))
        return;

    g_autoptr (GError) images_error = NULL;

    if (g_paste_storage_backend_delete_history_images (name, &images_error))
        return;

    /* Never over the store's own failure: the first thing that went wrong is
     * what says why, and a caller that asked for no error still has to hear
     * about images left on disk. */
    if (error && !*error)
        g_propagate_error (error, g_steal_pointer (&images_error));
    else
        g_warning ("Could not delete the images of \"%s\": %s", name, images_error->message);
}

/* The default list_histories: enumerate the history dir for files of a
 * flavour, the ones named with its @extension, so e.g. plain ".xml" and
 * encrypted ".xmls" histories never get mixed up. */
static GStrv
_g_paste_storage_list_histories (const gchar *extension,
                                 GError     **error)
{
    g_autoptr (GFile) history_dir = g_paste_util_get_history_dir ();
    g_autofree gchar *suffix = g_strconcat (".", extension, NULL);
    gsize suffix_len = strlen (suffix);
    /* A fresh profile has no history dir and lists nothing, which is an empty
     * list rather than a failure. A truncated listing is a failure and comes
     * back as NULL: "no histories" is exactly the answer that makes every
     * passphrase verify and a migration import nothing, then let its cleanup
     * delete the originals it never read.
     *
     * The name on disk, not the display one: what comes out of here is handed
     * straight back to g_paste_util_get_history_file_path() by read/delete/rekey,
     * and the display name is a UTF-8 rendering of the bytes — a file whose name
     * is not valid UTF-8 would come back under a name that names no file, and
     * every one of those callers would then act on nothing while reporting that
     * it acted. */
    g_auto (GStrv) files = g_paste_util_list_directory (history_dir, G_FILE_ATTRIBUTE_STANDARD_NAME, error);

    if (!files)
        return NULL;

    g_autoptr (GStrvBuilder) history_names = g_strv_builder_new ();

    for (GStrv file = files; *file; ++file)
    {
        if (!g_str_has_suffix (*file, suffix))
            continue;

        g_autofree gchar *name = g_strdup (*file);

        name[strlen (name) - suffix_len] = '\0';
        /* A stray file -- ".xml", or one an older GPaste wrote for an empty name
         * -- names nothing a history could be read, counted or deleted under. */
        if (g_paste_util_history_name_is_valid (name))
            g_strv_builder_take (history_names, g_steal_pointer (&name));
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
g_paste_storage_backend_list_histories (GPasteStorageBackend *self,
                                        GError              **error)
{
    g_return_val_if_fail (G_PASTE_IS_STORAGE_BACKEND (self), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    if (G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->list_histories)
        return G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->list_histories (self, error);

    return _g_paste_storage_list_histories (g_paste_storage_backend_get_extension (self), error);
}

/**
 * g_paste_storage_backend_count_history:
 * @self: a #GPasteStorageBackend instance
 * @name: the name of the history to count
 *
 * How many items the history called @name holds, without loading it where the
 * backend can tell without.
 *
 * Reading a history back to count it is what listing every history's size
 * costs, once per history, in the daemon's main loop; a backend that can ask
 * its store overrides this. The fallback reads and counts, so the answer is
 * the same either way.
 *
 * Returns: the number of items @name holds, and 0 for a history that is empty,
 *          absent or unreadable
 */
G_PASTE_VISIBLE gsize
g_paste_storage_backend_count_history (GPasteStorageBackend *self,
                                       const gchar          *name)
{
    g_return_val_if_fail (G_PASTE_IS_STORAGE_BACKEND (self), 0);
    g_return_val_if_fail (name, 0);

    if (G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->count_history)
        return G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->count_history (self, name);

    g_autolist (GPasteItem) history = NULL;
    gsize size;

    g_paste_storage_backend_read_history (self, name, NULL /* cancellable */, &history, &size);

    return g_list_length (history);
}

/**
 * g_paste_storage_backend_rekey:
 * @self: a #GPasteStorageBackend instance, holding the passphrase @name is
 *        currently encrypted with
 * @name: the name of the history to re-encrypt
 * @new_passphrase: the passphrase to encrypt @name with from now on
 * @split: (out) (optional): set to whether a failure left part of @name under
 *         @new_passphrase
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
 * A failure puts back what the re-key had already changed, so @name is left as
 * it was: a caller re-keying several histories can stop rather than split them
 * across two passphrases -- unless the filesystem refuses that too, in which
 * case @split is set and the originals that could not be put back are left
 * beside their files, the journal naming them.
 *
 * Returns: whether @name is now encrypted under @new_passphrase
 */
G_PASTE_VISIBLE gboolean
g_paste_storage_backend_rekey (GPasteStorageBackend *self,
                               const gchar          *name,
                               const gchar          *new_passphrase,
                               gboolean             *split)
{
    gboolean left_split = FALSE;

    if (split)
        *split = FALSE;

    g_return_val_if_fail (G_PASTE_IS_STORAGE_BACKEND (self), FALSE);
    g_return_val_if_fail (name, FALSE);
    g_return_val_if_fail (new_passphrase && *new_passphrase, FALSE);

    if (!G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->rekey)
    {
        g_warning ("This storage backend has no passphrase to change");
        return FALSE;
    }

    gboolean rekeyed = G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->rekey (self, name, new_passphrase, &left_split);

    if (split)
        *split = left_split;

    return rekeyed;
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

    G_PASTE_STORAGE_BACKEND_UPDATE (add_item, item, history);
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

    G_PASTE_STORAGE_BACKEND_UPDATE (remove_item, uuid);
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

    G_PASTE_STORAGE_BACKEND_UPDATE (replace_item, old_uuid, item);
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

    G_PASTE_STORAGE_BACKEND_UPDATE (clear_history);
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

/**
 * g_paste_storage_backend_get_settings:
 * @self: a #GPasteStorageBackend instance
 *
 * Get the #GPasteSettings @self was built with. Every backend shares the one
 * instance the caller created, so this is a plain accessor rather than a vfunc.
 *
 * Returns: (transfer none): the #GPasteSettings
 */
G_PASTE_VISIBLE GPasteSettings *
g_paste_storage_backend_get_settings (GPasteStorageBackend *self)
{
    g_return_val_if_fail (G_PASTE_IS_STORAGE_BACKEND (self), NULL);

    const GPasteStorageBackendPrivate *priv = g_paste_storage_backend_get_instance_private (self);

    return priv->settings;
}

static void
g_paste_storage_backend_class_init (GPasteStorageBackendClass *klass)
{
    klass->read_history_file = NULL;
    klass->write_history_file = NULL;
    klass->get_kind = NULL;
    klass->delete_history = NULL;
    klass->list_histories = NULL;
    klass->count_history = NULL;
    klass->rekey = NULL;
    klass->history_refutes_passphrase = NULL;

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

/* Every storage flavour, indexed by its kind: what it is called on disk, and
 * which class implements it. Adding a flavour is one row plus its enum value,
 * and every reader iterates up to %G_PASTE_N_STORAGE rather than carrying a
 * list of its own.
 *
 * @get_type for an encrypted flavour is the no-storage backend on purpose: the
 * encrypted ones are built through their own constructor below, and anything
 * reaching here with one has already failed to key it. Keeping the history in
 * memory is the only safe answer -- never plaintext on disk. */
static const struct
{
    const gchar *extension;
    GType      (*get_type) (void);
} storage_flavours[G_PASTE_N_STORAGE] = {
    [G_PASTE_STORAGE_NOOP]             = { "noop", g_paste_noop_backend_get_type },
    [G_PASTE_STORAGE_FILE]             = { "xml",  g_paste_file_backend_get_type },
    [G_PASTE_STORAGE_ENCRYPTED_FILE]   = { "xmls", g_paste_noop_backend_get_type },
#ifdef G_PASTE_ENABLE_SQLITE
    [G_PASTE_STORAGE_SQLITE]           = { "db",   g_paste_sqlite_backend_get_type },
#else
    /* Not built in: a plain kind may degrade to another plain kind. */
    [G_PASTE_STORAGE_SQLITE]           = { "db",   g_paste_file_backend_get_type },
#endif
    [G_PASTE_STORAGE_ENCRYPTED_SQLITE] = { "dbs",  g_paste_noop_backend_get_type },
};

static GType
_g_paste_storage_backend_get_type (GPasteStorage storage_kind)
{
    if (storage_kind >= G_PASTE_N_STORAGE || !storage_flavours[storage_kind].get_type)
        return G_PASTE_TYPE_FILE_BACKEND;

    return storage_flavours[storage_kind].get_type ();
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
    if (storage_kind >= G_PASTE_N_STORAGE || !storage_flavours[storage_kind].extension)
        return storage_flavours[G_PASTE_STORAGE_NOOP].extension;

    return storage_flavours[storage_kind].extension;
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
static GPasteStorageBackend *_g_paste_storage_backend_new_encrypted (GPasteStorage   storage_kind,
                                                                     GPasteSettings *settings,
                                                                     const gchar    *passphrase);

/**
 * g_paste_storage_passphrase_can_decrypt:
 * @storage_kind: the encrypted #GPasteStorage kind to verify against
 * @settings: a #GPasteSettings instance
 * @passphrase: the passphrase to check
 *
 * Check whether @passphrase actually unlocks the existing encrypted histories
 * of the @storage_kind flavor, so a wrong (or stale keyring) passphrase is never
 * accepted and given the chance to overwrite the real data. Nothing is written
 * to find out. The active history decides when it holds data, which may leave
 * others on another passphrase -- what a change of passphrase whose rollback
 * failed leaves -- and every history decides otherwise.
 *
 * Returns: %FALSE when the active history holds data @passphrase does not
 *          unlock, or holds none and another history does; and when the
 *          histories could not be listed at all -- an unverifiable passphrase
 *          is refused rather than assumed good
 */
G_PASTE_VISIBLE gboolean
g_paste_storage_passphrase_can_decrypt (GPasteStorage   storage_kind,
                                        GPasteSettings *settings,
                                        const gchar    *passphrase)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), FALSE);
    g_return_val_if_fail (passphrase && *passphrase, FALSE);

    g_autoptr (GPasteStorageBackend) backend = _g_paste_storage_backend_new_encrypted (storage_kind, settings, passphrase);

    /* A flavor this build cannot construct stores nothing to be wrong about. */
    if (!backend)
        return TRUE;

    const GPasteStorageBackendClass *klass = G_PASTE_STORAGE_BACKEND_GET_CLASS (backend);

    if (!klass->history_refutes_passphrase)
        return TRUE;
    /* The vfunc's contract: the one answers with the other. */
    g_return_val_if_fail (klass->history_confirms_passphrase, FALSE);

    g_autoptr (GError) error = NULL;
    g_auto (GStrv) names = g_paste_storage_backend_list_histories (backend, &error);

    /* Cannot read the store, so nothing refutes the passphrase and nothing
     * confirms it either. Refuse: the caller either prompts again or keeps the
     * history closed, where accepting would open it empty over real data. */
    if (!names)
    {
        g_warning ("Could not list the histories to verify the passphrase: %s", error->message);
        return FALSE;
    }

    /* The active history decides when it holds what this flavour encrypted:
     * the one the daemon loads, and the one a wrong passphrase would cost. The
     * others may be keyed differently -- what a change of passphrase whose
     * rollback failed leaves -- and stay closed under this one, read as
     * unreadable and so never written over: refusing a passphrase over them
     * would refuse every passphrase, and nothing could open again. */
    const gchar *active = g_paste_util_resolve_history_name (g_paste_settings_get_history_name (settings));

    if (klass->history_refutes_passphrase (backend, active))
        return FALSE;
    if (klass->history_confirms_passphrase (backend, active))
        return TRUE;

    /* Otherwise every history, not just up to the first that opens: with
     * nothing to go by in the active one -- absent, never written -- a
     * passphrase the others refute is a typo as likely as not, and the active
     * history written under it would be one more under a passphrase of its
     * own. */
    for (GStrv name = names; name && *name; ++name)
    {
        if (klass->history_refutes_passphrase (backend, *name))
            return FALSE;
    }

    return TRUE;
}

/**
 * g_paste_storage_passphrase_verifiable:
 * @storage_kind: an encrypted #GPasteStorage flavour
 * @settings: a #GPasteSettings instance
 *
 * Whether any history of @storage_kind holds what a wrong passphrase is refused
 * over: an encrypted stream past its header, or a database holding an item.
 * With none -- nothing stored yet, the placeholder a first read leaves, a
 * database emptied, which an open keys afresh for whatever it is given, a
 * foreign file -- g_paste_storage_passphrase_can_decrypt() accepts every
 * passphrase, and the one given keys everything written from then on.
 *
 * Returns: whether a wrong passphrase would be refused
 */
G_PASTE_VISIBLE gboolean
g_paste_storage_passphrase_verifiable (GPasteStorage   storage_kind,
                                       GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), FALSE);

    /* Asked of the files, by name: no key could be checked without the
     * passphrase this is about. */
    gboolean (*refutable) (const gchar *path);

    switch (storage_kind)
    {
    case G_PASTE_STORAGE_ENCRYPTED_FILE:
        refutable = g_paste_secret_stream_file_has_frame;
        break;
#ifdef G_PASTE_ENABLE_SQLITE
    case G_PASTE_STORAGE_ENCRYPTED_SQLITE:
        refutable = g_paste_sqlite_backend_database_has_items;
        break;
#endif
    default:
        return FALSE;
    }

    const gchar *extension = g_paste_storage_get_extension (storage_kind);
    g_auto (GStrv) names = _g_paste_storage_list_histories (extension, NULL);

    /* A store that cannot be listed refuses every passphrase (can_decrypt ()). */
    if (!names)
        return TRUE;

    for (GStrv name = names; *name; ++name)
    {
        g_autofree gchar *path = g_paste_util_get_history_file_path (*name, extension);

        if (refutable (path))
            return TRUE;
    }

    return FALSE;
}

/**
 * g_paste_storage_backend_refutes_passphrase:
 * @self: a #GPasteStorageBackend instance, built with the passphrase in question
 * @name: the name of a history
 *
 * Whether the history called @name proves @self's passphrase wrong: see
 * history_refutes_passphrase in #GPasteStorageBackendClass.
 *
 * Returns: whether @name refutes the passphrase
 */
G_PASTE_VISIBLE gboolean
g_paste_storage_backend_refutes_passphrase (GPasteStorageBackend *self,
                                            const gchar          *name)
{
    g_return_val_if_fail (G_PASTE_IS_STORAGE_BACKEND (self), FALSE);
    g_return_val_if_fail (name, FALSE);

    const GPasteStorageBackendClass *klass = G_PASTE_STORAGE_BACKEND_GET_CLASS (self);

    return klass->history_refutes_passphrase && klass->history_refutes_passphrase (self, name);
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
        if (g_paste_prompt_storage_is_offered (storage_kind))
            g_warning ("No passphrase for the encrypted storage backend; not storing the history");
        else
            g_warning ("This build cannot open the encrypted storage backend; not storing the history");
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
        g_paste_prompt_storage_is_offered (storage_kind) &&
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
