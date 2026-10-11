// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>
#include <gpaste-test-env.h>
#include <gpaste-3/gpaste-gsettings-keys.h>
#include <gpaste-daemon/gpaste-daemon-methods.h>
#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-file-backend.h>
#include <gpaste-daemon/gpaste-history.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-text-item.h>

#include <dlfcn.h>
#include <glib/gstdio.h>
#include <sys/file.h>
#include <fcntl.h>

/* Where the storage writes of the history named @name are held: a worker
 * writing it waits at the backend boundary until the case releases it, so the
 * case can act while that write is still out. The model and the saver stay the
 * shipped ones, their threads and completion callbacks included. A static
 * GMutex and GCond need no initialisation.
 *
 * With @require_written, a worker reading that history asserts the held write
 * has landed first. @password_name and @password_timeout are what the held
 * write found on the first password it was handed, once released. */
typedef struct
{
    GMutex       mutex;
    GCond        cond;
    GThread     *main_thread;
    const gchar *name;
    gboolean     hold_write;
    gboolean     started;
    gboolean     released;
    gboolean     require_written;
    gboolean     written;
    gchar       *password_name;
    guint        password_timeout;
} Barrier;

static Barrier barrier;
static gboolean (*real_write) (GPasteStorageBackend *, const gchar *, const GList *);
static gboolean (*real_read) (GPasteStorageBackend *, const gchar *, GCancellable *, GList **, gsize *);

gboolean
g_paste_storage_backend_write_history (GPasteStorageBackend *backend,
                                       const gchar          *name,
                                       const GList          *items)
{
    gboolean held = FALSE;

    /* The main thread is never held: a drain applying writes there is what the
     * held worker is being raced against. */
    if (g_thread_self () != barrier.main_thread)
    {
        g_mutex_lock (&barrier.mutex);
        held = barrier.hold_write && g_paste_str_equal (name, barrier.name);
        if (held)
        {
            barrier.started = TRUE;
            g_cond_broadcast (&barrier.cond);
            while (!barrier.released)
                g_cond_wait (&barrier.cond, &barrier.mutex);

            for (const GList *item = items; item; item = item->next)
            {
                if (G_PASTE_IS_PASSWORD_ITEM (item->data))
                {
                    g_set_str (&barrier.password_name, g_paste_password_item_get_name (item->data));
                    barrier.password_timeout = g_paste_password_item_get_timeout (item->data);
                    break;
                }
            }
        }
        g_mutex_unlock (&barrier.mutex);
    }

    gboolean written = real_write (backend, name, items);

    if (held)
    {
        g_mutex_lock (&barrier.mutex);
        barrier.written = TRUE;
        g_mutex_unlock (&barrier.mutex);
    }

    return written;
}

gboolean
g_paste_storage_backend_read_history (GPasteStorageBackend *backend,
                                      const gchar          *name,
                                      GCancellable         *cancellable,
                                      GList               **items,
                                      gsize                *size)
{
    if (g_thread_self () != barrier.main_thread)
    {
        g_mutex_lock (&barrier.mutex);
        if (barrier.require_written && g_paste_str_equal (name, barrier.name))
            g_assert_true (barrier.written);
        g_mutex_unlock (&barrier.mutex);
    }

    return real_read (backend, name, cancellable, items, size);
}

static void
wait_worker (void)
{
    gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

    g_mutex_lock (&barrier.mutex);
    while (!barrier.started)
        g_assert_true (g_cond_wait_until (&barrier.cond, &barrier.mutex, deadline));
    g_mutex_unlock (&barrier.mutex);
}

static void
release_worker (void)
{
    g_mutex_lock (&barrier.mutex);
    barrier.released = TRUE;
    g_cond_broadcast (&barrier.cond);
    g_mutex_unlock (&barrier.mutex);
}

/* For a call that has to wait for the held write: it blocks the main thread
 * until then, so the release comes from elsewhere. */
static gpointer
release_later (gpointer data G_GNUC_UNUSED)
{
    g_usleep (100 * 1000);
    release_worker ();
    return NULL;
}

/* For a call that must not wait for the held write: one that does gets it
 * released five seconds on, and comes back to find it released -- a failure
 * rather than a hang. */
static gpointer
release_eventually (gpointer data G_GNUC_UNUSED)
{
    gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

    g_mutex_lock (&barrier.mutex);
    while (!barrier.released && g_cond_wait_until (&barrier.cond, &barrier.mutex, deadline))
        ;
    g_mutex_unlock (&barrier.mutex);
    release_worker ();
    return NULL;
}

static gboolean
history_loaded (gconstpointer data,
                gconstpointer arg G_GNUC_UNUSED)
{
    return !g_paste_history_is_loading ((GPasteHistory *) data);
}

static void
wait_load (GPasteHistory *history)
{
    g_paste_test_bus_wait_until (history_loaded, history, NULL);
    g_assert_false (g_paste_history_is_loading (history));
}

static gboolean
is_released (void)
{
    g_mutex_lock (&barrier.mutex);
    gboolean released = barrier.released;
    g_mutex_unlock (&barrier.mutex);

    return released;
}

static void
finish_history (GPasteHistory *history)
{
    g_paste_history_flush (history);
    while (g_main_context_iteration (NULL, FALSE))
        ;
}

/* A history called @name holding one stored item, loaded, with nothing held. */
static void
fixture (GPasteSettings       **settings,
         GPasteStorageBackend **backend,
         GPasteHistory        **history,
         const gchar           *name)
{
    *settings = g_paste_settings_new ();

    /* The keys the history and its storage read, back to the schema's defaults:
     * the memory backend is shared by the whole binary, so a key one case set
     * would otherwise reach every case after it. A key the code under test
     * comes to read belongs here too. */
    static const gchar *reset_keys[] = {
        G_PASTE_GROWING_LINES_SETTING, G_PASTE_HISTORY_NAME_SETTING, G_PASTE_IMAGES_SUPPORT_SETTING,
        G_PASTE_MAX_HISTORY_SIZE_SETTING, G_PASTE_MAX_MEMORY_USAGE_SETTING, G_PASTE_MAX_TEXT_ITEM_SIZE_SETTING,
        G_PASTE_MIN_TEXT_ITEM_SIZE_SETTING, G_PASTE_PASSWORD_TIMEOUT_SETTING, G_PASTE_RICH_TEXT_SUPPORT_SETTING,
        G_PASTE_TRIM_ITEMS_SETTING,
    };

    for (guint i = 0; i < G_N_ELEMENTS (reset_keys); ++i)
        g_paste_settings_reset (*settings, reset_keys[i]);
    g_paste_settings_set_growing_lines (*settings, FALSE);
    g_paste_settings_set_storage_backend (*settings, G_PASTE_STORAGE_FILE);
    *backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, *settings);

    g_autolist (GPasteItem) old = g_list_prepend (NULL, g_paste_text_item_new ("old stored item"));

    real_write (*backend, name, old);
    *history = g_paste_history_new (*settings);
    g_paste_history_load (*history, name);

    g_mutex_lock (&barrier.mutex);
    barrier.name = name;
    barrier.hold_write = FALSE;
    barrier.started = FALSE;
    barrier.released = FALSE;
    barrier.require_written = FALSE;
    barrier.written = FALSE;
    g_clear_pointer (&barrier.password_name, g_free);
    barrier.password_timeout = 0;
    g_mutex_unlock (&barrier.mutex);
}

/* What @name holds on disk, as test-history's read_history_ok () reads it: a
 * test binary keeps the few storage helpers it needs to itself. */
static GList *
read_items (GPasteStorageBackend *backend,
            const gchar          *name)
{
    g_autolist (GPasteItem) items = NULL;
    gsize size = 0;

    g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &items, &size));
    return g_steal_pointer (&items);
}

/* A history switched away from with its writes still out, one in flight and
 * one queued, then deleted: neither may land after the unlink. */
static void
test_delete_pending (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = NULL;

    fixture (&settings, &backend, &history, "deleted-source");
    barrier.hold_write = TRUE;
    g_paste_history_add (history, g_paste_text_item_new ("first pending"));
    wait_worker ();
    g_paste_history_add (history, g_paste_text_item_new ("second pending"));
    g_paste_history_load (history, "other-current");

    g_autoptr (GError) error = NULL;
    g_autoptr (GThread) release = g_thread_new ("release-delete-write", release_later, NULL);

    g_assert_true (g_paste_history_delete (history, "deleted-source", &error));
    g_thread_join (g_steal_pointer (&release));
    g_assert_no_error (error);
    finish_history (history);

    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (backend, "deleted-source");

    g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));
}

/* Deleting another history has nothing to wait for in the current one's
 * writes, which carry on and land. */
static void
test_delete_other (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = NULL;

    fixture (&settings, &backend, &history, "kept-current");

    g_autolist (GPasteItem) other = g_list_prepend (NULL, g_paste_text_item_new ("other stored item"));

    real_write (backend, "deleted-other", other);
    barrier.hold_write = TRUE;
    g_paste_history_add (history, g_paste_text_item_new ("pending"));
    wait_worker ();

    g_autoptr (GError) error = NULL;
    g_autoptr (GThread) release = g_thread_new ("release-current-write", release_eventually, NULL);

    g_assert_true (g_paste_history_delete (history, "deleted-other", &error));
    g_assert_no_error (error);
    g_assert_false (is_released ());
    release_worker ();
    g_thread_join (g_steal_pointer (&release));
    finish_history (history);

    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (backend, "deleted-other");
    g_autolist (GPasteItem) items = read_items (backend, "kept-current");

    g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));
    g_assert_cmpuint (g_list_length (items), ==, 2);
    g_assert_cmpstr (g_paste_item_get_real_value (items->data), ==, "pending");
}

/* A history switched away from and straight back to while its writes are out
 * is read once they have landed: read earlier, it would be installed without
 * them and saved over them by the switch's own full write. */
static void
test_switch_pending (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = NULL;

    fixture (&settings, &backend, &history, "switch-source");
    barrier.hold_write = TRUE;
    g_paste_history_add (history, g_paste_text_item_new ("first pending"));
    wait_worker ();
    g_paste_history_add (history, g_paste_text_item_new ("second pending"));
    g_mutex_lock (&barrier.mutex);
    barrier.require_written = TRUE;
    g_mutex_unlock (&barrier.mutex);

    g_paste_history_switch (history, "other-switch");
    g_paste_history_switch (history, "switch-source");

    g_autoptr (GThread) release = g_thread_new ("release-switch-write", release_later, NULL);

    wait_load (history);
    g_thread_join (g_steal_pointer (&release));
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 3);
    g_assert_cmpstr (g_paste_item_get_real_value (g_paste_history_get (history, 0)), ==, "second pending");
    finish_history (history);

    g_autolist (GPasteItem) items = read_items (backend, "switch-source");

    g_assert_cmpuint (g_list_length (items), ==, 3);
    g_assert_cmpstr (g_paste_item_get_real_value (items->data), ==, "second pending");
}

/* Another history is read whatever the one left still has out: with the file
 * backend each is a store of its own. And the writes it has out still land,
 * the one queued included, although the history switched to saves itself as
 * soon as it is loaded. */
static void
test_switch_other (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = NULL;

    fixture (&settings, &backend, &history, "switch-left");
    barrier.hold_write = TRUE;
    g_paste_history_add (history, g_paste_text_item_new ("first pending"));
    wait_worker ();
    g_paste_history_add (history, g_paste_text_item_new ("second pending"));

    g_autoptr (GThread) release = g_thread_new ("release-left-write", release_eventually, NULL);

    g_paste_history_switch (history, "switch-reached");
    wait_load (history);
    g_assert_false (is_released ());
    g_assert_cmpstr (g_paste_history_get_current (history), ==, "switch-reached");
    release_worker ();
    g_thread_join (g_steal_pointer (&release));
    finish_history (history);

    g_autolist (GPasteItem) items = read_items (backend, "switch-left");

    g_assert_cmpuint (g_list_length (items), ==, 3);
    g_assert_cmpstr (g_paste_item_get_real_value (items->data), ==, "second pending");
}

/* A history left with its writes out, switched to another one which loads and
 * saves itself meanwhile: what the switch leaves the case to act on. */
static void
leave_with_write_out (GPasteHistory *history,
                      const gchar   *next)
{
    barrier.hold_write = TRUE;
    g_paste_history_add (history, g_paste_text_item_new ("accepted"));
    wait_worker ();
    g_paste_history_switch (history, next);
    wait_load (history);
}

/* The current history is backed up from its model, which holds what is still
 * on its way to the store, without waiting for it to get there. */
static void
test_backup_current (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = NULL;

    fixture (&settings, &backend, &history, "backup-current");
    barrier.hold_write = TRUE;
    g_paste_history_add (history, g_paste_text_item_new ("accepted"));
    wait_worker ();

    g_autoptr (GPasteDaemon3) skeleton = g_paste_daemon3_skeleton_new ();
    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .skeleton = skeleton };
    g_autoptr (GError) error = NULL;
    g_autoptr (GThread) release = g_thread_new ("release-current-write", release_eventually, NULL);

    g_paste_daemon_methods_backup_history (&methods, "backup-current", "backup-current-copy", &error);
    g_assert_no_error (error);
    g_assert_false (is_released ());
    release_worker ();
    g_thread_join (g_steal_pointer (&release));
    finish_history (history);

    g_autolist (GPasteItem) items = read_items (backend, "backup-current-copy");

    g_assert_cmpuint (g_list_length (items), ==, 2);
    g_assert_cmpstr (g_paste_item_get_real_value (items->data), ==, "accepted");
}

/* Another history is backed up from its store, once its writes still out have
 * landed there. */
static void
test_backup_other (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = NULL;

    fixture (&settings, &backend, &history, "backup-left");
    leave_with_write_out (history, "backup-reached");

    g_autoptr (GPasteDaemon3) skeleton = g_paste_daemon3_skeleton_new ();
    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .skeleton = skeleton };
    g_autoptr (GError) error = NULL;
    g_autoptr (GThread) release = g_thread_new ("release-left-write", release_later, NULL);

    g_paste_daemon_methods_backup_history (&methods, "backup-left", "backup-left-copy", &error);
    g_thread_join (g_steal_pointer (&release));
    g_assert_no_error (error);
    finish_history (history);

    g_autolist (GPasteItem) items = read_items (backend, "backup-left-copy");

    g_assert_cmpuint (g_list_length (items), ==, 2);
    g_assert_cmpstr (g_paste_item_get_real_value (items->data), ==, "accepted");
}

/* The current history has no model to back up from while it loads -- here
 * waiting for its own write to land, having been switched away from and back. */
static void
test_backup_loading (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = NULL;

    fixture (&settings, &backend, &history, "backup-loading");
    leave_with_write_out (history, "backup-loading-elsewhere");
    g_paste_history_switch (history, "backup-loading");
    g_assert_true (g_paste_history_is_loading (history));

    g_autoptr (GPasteDaemon3) skeleton = g_paste_daemon3_skeleton_new ();
    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .skeleton = skeleton };
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_backup_history (&methods, "backup-loading", "backup-loading-copy", &error);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
    release_worker ();
    wait_load (history);
    finish_history (history);

    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (backend, "backup-loading-copy");

    g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));
}

/* Emptying the current history goes through its model, its write queued behind
 * the one out: nothing to wait for. */
static void
test_empty_current (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = NULL;

    fixture (&settings, &backend, &history, "empty-current");
    barrier.hold_write = TRUE;
    g_paste_history_add (history, g_paste_text_item_new ("accepted"));
    wait_worker ();

    g_autoptr (GPasteDaemon3) skeleton = g_paste_daemon3_skeleton_new ();
    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .skeleton = skeleton };
    g_autoptr (GError) error = NULL;
    g_autoptr (GThread) release = g_thread_new ("release-current-write", release_eventually, NULL);

    g_paste_daemon_methods_empty_history (&methods, "empty-current", &error);
    g_assert_no_error (error);
    g_assert_false (is_released ());
    release_worker ();
    g_thread_join (g_steal_pointer (&release));
    finish_history (history);

    g_autolist (GPasteItem) items = read_items (backend, "empty-current");

    g_assert_null (items);
}

/* Another history is written over empty once its writes still out have landed:
 * landing afterwards, they would bring back what was emptied. */
static void
test_empty_other (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = NULL;

    fixture (&settings, &backend, &history, "empty-left");
    leave_with_write_out (history, "empty-reached");

    g_autoptr (GPasteDaemon3) skeleton = g_paste_daemon3_skeleton_new ();
    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .skeleton = skeleton };
    g_autoptr (GError) error = NULL;
    g_autoptr (GThread) release = g_thread_new ("release-left-write", release_later, NULL);

    g_paste_daemon_methods_empty_history (&methods, "empty-left", &error);
    g_thread_join (g_steal_pointer (&release));
    g_assert_no_error (error);
    finish_history (history);

    g_autolist (GPasteItem) items = read_items (backend, "empty-left");

    g_assert_null (items);
}

/* Once the store has been handed over, neither a backup nor an emptying
 * touches it. */
static void
test_stopped (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = NULL;

    fixture (&settings, &backend, &history, "stopped-target");
    g_paste_history_load (history, "stopped-current");
    finish_history (history);

    g_autoptr (GPasteDaemon3) skeleton = g_paste_daemon3_skeleton_new ();
    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .skeleton = skeleton };

    {
        g_autoptr (GError) error = NULL;

        g_paste_daemon_methods_empty_history (&methods, "stopped-target", &error);
        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
    }

    {
        g_autoptr (GError) error = NULL;

        g_paste_daemon_methods_backup_history (&methods, "stopped-target", "stopped-copy", &error);
        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
    }

    /* A change to an entry is refused as much: recorded nowhere, the reload
     * that ends the handover would undo it -- and a switch, which would drop
     * what was copied meanwhile. An add, which the reload keeps, is not
     * (/clipboard/add_handed_over). */
    {
        g_autoptr (GError) error = NULL;

        g_paste_daemon_methods_switch_history (&methods, "switched-while-handed-over", &error);
        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
        g_assert_cmpstr (g_paste_history_get_current (history), ==, "stopped-current");
        g_clear_error (&error);

        g_paste_daemon_methods_delete_item (&methods, "00000000-0000-4000-8000-000000000000", &error);
        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
        g_clear_error (&error);

        g_paste_daemon_methods_delete_password (&methods, "stopped password", &error);
        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
        g_clear_error (&error);

        g_paste_daemon_methods_set_favourite (&methods, "00000000-0000-4000-8000-000000000000", TRUE, &error);
        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
        g_clear_error (&error);

        g_paste_daemon_methods_select (&methods, "00000000-0000-4000-8000-000000000000", &error);
        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
        g_clear_error (&error);

        g_autofree gchar *replaced = g_paste_daemon_methods_replace (&methods, "00000000-0000-4000-8000-000000000000", "contents", &error);

        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
        g_clear_error (&error);

        g_paste_daemon_methods_strip_rich_text (&methods, "00000000-0000-4000-8000-000000000000", &error);
        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
        g_clear_error (&error);

        g_autofree gchar *made = g_paste_daemon_methods_make_password (&methods, "00000000-0000-4000-8000-000000000000", "name", 0, &error);

        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
        g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);
    }

    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (backend, "stopped-copy");
    g_autolist (GPasteItem) items = read_items (backend, "stopped-target");

    g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));
    g_assert_cmpuint (g_list_length (items), ==, 1);
    g_assert_cmpstr (g_paste_item_get_real_value (items->data), ==, "old stored item");
}

/* A history just left counts what its writes still out are about to store,
 * not what its store holds yet. */
static void
test_count_pending (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = NULL;

    fixture (&settings, &backend, &history, "count-left");
    leave_with_write_out (history, "count-reached");

    g_assert_cmpuint (g_paste_history_get_size (history, "count-left"), ==, 2);
    release_worker ();
    finish_history (history);
    g_assert_cmpuint (g_paste_history_get_size (history, "count-left"), ==, 2);
}

/* What the store answers for of a history just left is what it is counted by
 * from settings changed since the snapshot was taken, too: no image with
 * images-support off, and no more than max-history-size items. */
static void
test_count_pending_settings (gconstpointer data)
{
    gboolean images = GPOINTER_TO_INT (data);
    const gchar *name = (images) ? "count-images" : "count-capped";
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = NULL;

    fixture (&settings, &backend, &history, name);
    g_paste_settings_set_images_support (settings, TRUE);
    if (images)
    {
        const guchar pixel[] = { 50, 51, 52, 255 };
        g_autoptr (GBytes) bytes = g_bytes_new (pixel, sizeof (pixel));
        g_autoptr (GdkTexture) texture = gdk_memory_texture_new (1, 1, GDK_MEMORY_R8G8B8A8, bytes, 4);

        g_paste_history_add (history, g_paste_image_item_new (texture));
    }
    else
    {
        for (guint i = 0; i < 6; ++i)
        {
            g_autofree gchar *text = g_strdup_printf ("text %u", i);

            g_paste_history_add (history, g_paste_text_item_new (text));
        }
    }
    finish_history (history);
    g_paste_history_resume (history);
    leave_with_write_out (history, (images) ? "count-images-reached" : "count-capped-reached");

    /* 3 items, the image left out; 8, capped at 5. */
    if (images)
        g_paste_settings_set_images_support (settings, FALSE);
    else
        g_paste_settings_set_max_history_size (settings, 5);

    guint64 expected = (images) ? 2 : 5;

    g_assert_cmpuint (g_paste_history_get_size (history, name), ==, expected);
    release_worker ();
    finish_history (history);
    g_assert_cmpuint (g_paste_history_get_size (history, name), ==, expected);
}

/* What a plain store keeps of a history just left is what it is counted by: its
 * passwords, which that store's write leaves out, are not counted. */
static void
test_count_pending_passwords (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = NULL;

    fixture (&settings, &backend, &history, "count-passwords");
    g_paste_history_add (history, g_paste_password_item_new ("left out", "secret", 0));
    /* Landed before the write held next is queued: queued behind one still in
     * flight, it would start only from the main loop, which nothing iterates
     * while the case waits for it. */
    finish_history (history);
    g_paste_history_resume (history);
    leave_with_write_out (history, "count-passwords-reached");

    g_assert_cmpuint (g_paste_history_get_size (history, "count-passwords"), ==, 2);
    release_worker ();
    finish_history (history);
    g_assert_cmpuint (g_paste_history_get_size (history, "count-passwords"), ==, 2);
}

/* A password renamed while a write holding it is out: the write stores the
 * name and timeout it was recorded with, the rename putting a new item in its
 * place rather than writing over the one a worker reads. */
static void
test_rename_during_write (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = NULL;

    fixture (&settings, &backend, &history, "rename-during-write");

    GPasteItem *password = g_paste_password_item_new ("original", "secret", 42);
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (password));

    barrier.hold_write = TRUE;
    g_paste_history_add (history, password);
    wait_worker ();

    g_autofree gchar *renamed = g_paste_history_make_password (history, uuid, "renamed", 7);

    g_assert_cmpstr (renamed, ==, uuid);
    release_worker ();
    finish_history (history);

    g_assert_cmpstr (barrier.password_name, ==, "original");
    g_assert_cmpuint (barrier.password_timeout, ==, 42);

    GPasteItem *kept = g_paste_history_get_by_uuid (history, uuid);

    g_assert_cmpstr (g_paste_password_item_get_name (G_PASTE_PASSWORD_ITEM (kept)), ==, "renamed");
    g_assert_cmpuint (g_paste_password_item_get_timeout (G_PASTE_PASSWORD_ITEM (kept)), ==, 7);
}

/* An image deleted while a write still holding it is out leaves no file once
 * the write its deletion records has landed. Deleted when the item left the
 * model, the file would come back with the write that was out, and the next
 * one, no longer referencing it, would never take it again. */
static void
test_remove_image_during_write (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = NULL;

    fixture (&settings, &backend, &history, "image-removed-during-write");
    g_paste_settings_set_images_support (settings, TRUE);

    const guchar pixel[] = { 40, 41, 42, 255 };
    g_autoptr (GBytes) bytes = g_bytes_new (pixel, sizeof (pixel));
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (1, 1, GDK_MEMORY_R8G8B8A8, bytes, 4);
    GPasteItem *image = g_paste_image_item_new (texture);
    g_autofree gchar *path = g_paste_file_backend_image_path ("image-removed-during-write",
                                                              g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (image)));

    barrier.hold_write = TRUE;
    g_paste_history_add (history, image);
    wait_worker ();
    g_paste_history_remove (history, 0);
    release_worker ();
    finish_history (history);

    g_autolist (GPasteItem) items = read_items (backend, "image-removed-during-write");

    g_assert_cmpuint (g_list_length (items), ==, 1);
    g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));
}

/* A load left pending behind a write when the history is handed over waits for
 * the handover to end: started as that write lands, it would take back the
 * storage lock the host has just let go of. */
static void
test_flush_holds_pending_load (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = NULL;

    fixture (&settings, &backend, &history, "flush-pending");
    leave_with_write_out (history, "flush-pending-elsewhere");
    g_paste_history_switch (history, "flush-pending");
    g_assert_true (g_paste_history_is_loading (history));

    g_autoptr (GThread) release = g_thread_new ("release-flush-write", release_later, NULL);

    g_paste_history_flush (history);
    g_thread_join (g_steal_pointer (&release));
    g_paste_storage_backend_unlock ();
    g_paste_test_bus_pump (500);

    /* Another open file, which flock () tells apart even in this process. */
    g_autofree gchar *dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *lock = g_build_filename (dir, "lock", NULL);
    gint fd = g_open (lock, O_RDWR | O_CREAT | O_CLOEXEC, 0600);

    g_assert_cmpint (fd, >=, 0);
    g_assert_cmpint (flock (fd, LOCK_EX | LOCK_NB), ==, 0);
    g_close (fd, NULL);
}

int
main (int    argc,
      char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);

    barrier.main_thread = g_thread_self ();
    real_write = dlsym (RTLD_NEXT, "g_paste_storage_backend_write_history");
    real_read = dlsym (RTLD_NEXT, "g_paste_storage_backend_read_history");
    g_assert_nonnull (real_write);
    g_assert_nonnull (real_read);

    g_test_add_func ("/history-ordering/delete-pending", test_delete_pending);
    g_test_add_func ("/history-ordering/delete-other", test_delete_other);
    g_test_add_func ("/history-ordering/switch-pending", test_switch_pending);
    g_test_add_func ("/history-ordering/switch-other", test_switch_other);
    g_test_add_func ("/history-ordering/backup-current", test_backup_current);
    g_test_add_func ("/history-ordering/backup-other", test_backup_other);
    g_test_add_func ("/history-ordering/backup-loading", test_backup_loading);
    g_test_add_func ("/history-ordering/empty-current", test_empty_current);
    g_test_add_func ("/history-ordering/empty-other", test_empty_other);
    g_test_add_func ("/history-ordering/stopped", test_stopped);
    g_test_add_func ("/history-ordering/count-pending", test_count_pending);
    g_test_add_func ("/history-ordering/count-pending-passwords", test_count_pending_passwords);
    g_test_add_data_func ("/history-ordering/count-pending-images-off", GINT_TO_POINTER (TRUE), test_count_pending_settings);
    g_test_add_data_func ("/history-ordering/count-pending-capped", GINT_TO_POINTER (FALSE), test_count_pending_settings);
    g_test_add_func ("/history-ordering/rename-during-write", test_rename_during_write);
    g_test_add_func ("/history-ordering/remove-image-during-write", test_remove_image_during_write);
    g_test_add_func ("/history-ordering/flush-holds-pending-load", test_flush_holds_pending_load);

    return g_paste_test_env_run ();
}
