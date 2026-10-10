// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-env.h>
#include <gpaste-3/gpaste-gsettings-keys.h>
#include <gpaste-daemon/gpaste-history.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-text-item.h>

#include <dlfcn.h>

/* Where the storage writes of the history named @name are held: a worker
 * writing it waits at the backend boundary until the case releases it, so the
 * case can act while that write is still out. The model and the saver stay the
 * shipped ones, their threads and completion callbacks included. A static
 * GMutex and GCond need no initialisation. */
typedef struct
{
    GMutex       mutex;
    GCond        cond;
    GThread     *main_thread;
    const gchar *name;
    gboolean     hold_write;
    gboolean     started;
    gboolean     released;
} Barrier;

static Barrier barrier;
static void (*real_write) (GPasteStorageBackend *, const gchar *, const GList *);

void
g_paste_storage_backend_write_history (GPasteStorageBackend *backend,
                                       const gchar          *name,
                                       const GList          *items)
{
    /* The main thread is never held: a drain applying writes there is what the
     * held worker is being raced against. */
    if (g_thread_self () != barrier.main_thread)
    {
        g_mutex_lock (&barrier.mutex);
        if (barrier.hold_write && g_paste_str_equal (name, barrier.name))
        {
            barrier.started = TRUE;
            g_cond_broadcast (&barrier.cond);
            while (!barrier.released)
                g_cond_wait (&barrier.cond, &barrier.mutex);
        }
        g_mutex_unlock (&barrier.mutex);
    }

    real_write (backend, name, items);
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

int
main (int    argc,
      char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);

    barrier.main_thread = g_thread_self ();
    real_write = dlsym (RTLD_NEXT, "g_paste_storage_backend_write_history");
    g_assert_nonnull (real_write);

    g_test_add_func ("/history-ordering/delete-pending", test_delete_pending);
    g_test_add_func ("/history-ordering/delete-other", test_delete_other);

    return g_paste_test_env_run ();
}
