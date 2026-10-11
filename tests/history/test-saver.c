// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-env.h>
#include <gpaste-daemon/gpaste-history-saver.h>
#include <gpaste-daemon/gpaste-text-item.h>

/* A storage backend rewriting its histories whole, as the file backend does,
 * whose writes wait to be released and remember the head each one stored. A
 * subclass of its own, where test-ordering holds the shipped backend's writes
 * by interposing them: this suite drives the saver with no history around it,
 * and asks what reached the backend rather than the disk. */
#define TEST_TYPE_BACKEND (test_backend_get_type ())
G_DECLARE_FINAL_TYPE (TestBackend, test_backend, TEST, BACKEND, GPasteStorageBackend)

struct _TestBackend
{
    GPasteStorageBackend parent_instance;

    GMutex      mutex;
    GCond       cond;
    gboolean    started;
    gboolean    released;
    guint       writes;
    GHashTable *values;
};

G_DEFINE_TYPE (TestBackend, test_backend, G_PASTE_TYPE_STORAGE_BACKEND)

static GPasteStorage
test_backend_kind (GPasteStorageBackend *backend G_GNUC_UNUSED)
{
    return G_PASTE_STORAGE_FILE;
}

static gboolean
test_backend_write (GPasteStorageBackend *backend,
                    const gchar          *name,
                    const GList          *items)
{
    TestBackend *self = TEST_BACKEND (backend);

    g_mutex_lock (&self->mutex);
    self->started = TRUE;
    g_cond_broadcast (&self->cond);
    while (!self->released)
        g_cond_wait (&self->cond, &self->mutex);

    ++self->writes;
    if (items)
        g_hash_table_replace (self->values, g_strdup (name), g_strdup (g_paste_item_get_real_value (items->data)));
    g_mutex_unlock (&self->mutex);

    return TRUE;
}

static void
test_backend_dispose (GObject *object)
{
    TestBackend *self = TEST_BACKEND (object);

    g_clear_pointer (&self->values, g_hash_table_unref);
    G_OBJECT_CLASS (test_backend_parent_class)->dispose (object);
}

static void
test_backend_finalize (GObject *object)
{
    TestBackend *self = TEST_BACKEND (object);

    g_mutex_clear (&self->mutex);
    g_cond_clear (&self->cond);
    G_OBJECT_CLASS (test_backend_parent_class)->finalize (object);
}

static void
test_backend_class_init (TestBackendClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GPasteStorageBackendClass *backend_class = G_PASTE_STORAGE_BACKEND_CLASS (klass);

    object_class->dispose = test_backend_dispose;
    object_class->finalize = test_backend_finalize;
    backend_class->get_kind = test_backend_kind;
    backend_class->write_history_file = test_backend_write;
}

static void
test_backend_init (TestBackend *self)
{
    g_mutex_init (&self->mutex);
    g_cond_init (&self->cond);
    self->values = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

/* The same store applied one operation at a time, as the SQLite flavours are:
 * its operations wait to be released, and its reads are counted. */
#define TEST_TYPE_INCREMENTAL (test_incremental_get_type ())
G_DECLARE_FINAL_TYPE (TestIncremental, test_incremental, TEST, INCREMENTAL, GPasteStorageBackend)

struct _TestIncremental
{
    GPasteStorageBackend parent_instance;

    GMutex   mutex;
    GCond    cond;
    gboolean started;
    gboolean released;
    guint    reads;
};

G_DEFINE_TYPE (TestIncremental, test_incremental, G_PASTE_TYPE_STORAGE_BACKEND)

static void
test_incremental_hold (GPasteStorageBackend *backend)
{
    TestIncremental *self = TEST_INCREMENTAL (backend);

    g_mutex_lock (&self->mutex);
    self->started = TRUE;
    g_cond_broadcast (&self->cond);
    while (!self->released)
        g_cond_wait (&self->cond, &self->mutex);
    g_mutex_unlock (&self->mutex);
}

static gboolean
test_incremental_read (GPasteStorageBackend *backend,
                       const gchar          *name        G_GNUC_UNUSED,
                       GCancellable         *cancellable G_GNUC_UNUSED,
                       GList               **history,
                       gsize                *size)
{
    TestIncremental *self = TEST_INCREMENTAL (backend);

    g_mutex_lock (&self->mutex);
    ++self->reads;
    g_mutex_unlock (&self->mutex);
    *history = NULL;
    *size = 0;

    return TRUE;
}

static gboolean
test_incremental_write (GPasteStorageBackend *backend,
                        const gchar          *name    G_GNUC_UNUSED,
                        const GList          *history G_GNUC_UNUSED)
{
    test_incremental_hold (backend);

    return TRUE;
}

static void
test_incremental_add (GPasteStorageBackend *backend,
                      const gchar          *name    G_GNUC_UNUSED,
                      GPasteItem           *item    G_GNUC_UNUSED,
                      const GList          *history G_GNUC_UNUSED)
{
    test_incremental_hold (backend);
}

static void
test_incremental_remove (GPasteStorageBackend *backend,
                         const gchar          *name G_GNUC_UNUSED,
                         const gchar          *uuid G_GNUC_UNUSED)
{
    test_incremental_hold (backend);
}

static void
test_incremental_replace (GPasteStorageBackend *backend,
                          const gchar          *name     G_GNUC_UNUSED,
                          const gchar          *old_uuid G_GNUC_UNUSED,
                          GPasteItem           *item     G_GNUC_UNUSED)
{
    test_incremental_hold (backend);
}

static void
test_incremental_clear (GPasteStorageBackend *backend,
                        const gchar          *name G_GNUC_UNUSED)
{
    test_incremental_hold (backend);
}

static void
test_incremental_finalize (GObject *object)
{
    TestIncremental *self = TEST_INCREMENTAL (object);

    g_mutex_clear (&self->mutex);
    g_cond_clear (&self->cond);
    G_OBJECT_CLASS (test_incremental_parent_class)->finalize (object);
}

static void
test_incremental_class_init (TestIncrementalClass *klass)
{
    GPasteStorageBackendClass *backend_class = G_PASTE_STORAGE_BACKEND_CLASS (klass);

    G_OBJECT_CLASS (klass)->finalize = test_incremental_finalize;
    backend_class->get_kind = test_backend_kind;
    backend_class->read_history_file = test_incremental_read;
    backend_class->write_history_file = test_incremental_write;
    backend_class->add_item = test_incremental_add;
    backend_class->remove_item = test_incremental_remove;
    backend_class->replace_item = test_incremental_replace;
    backend_class->clear_history = test_incremental_clear;
}

static void
test_incremental_init (TestIncremental *self)
{
    g_mutex_init (&self->mutex);
    g_cond_init (&self->cond);
}

static void
on_loaded (gpointer owner G_GNUC_UNUSED,
           GList   *history,
           gsize    size G_GNUC_UNUSED,
           gboolean save_after G_GNUC_UNUSED,
           gboolean readable G_GNUC_UNUSED)
{
    g_autolist (GPasteItem) items = history;
}

static void
on_diverged (gpointer     owner G_GNUC_UNUSED,
             const gchar *name G_GNUC_UNUSED)
{
}

static void
wait_for_worker (TestBackend *backend)
{
    gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

    g_mutex_lock (&backend->mutex);
    while (!backend->started)
        g_assert_true (g_cond_wait_until (&backend->cond, &backend->mutex, deadline));
    g_mutex_unlock (&backend->mutex);
}

static void
finish_worker (TestBackend        *backend,
               GPasteHistorySaver *saver)
{
    g_mutex_lock (&backend->mutex);
    backend->released = TRUE;
    g_cond_broadcast (&backend->cond);
    g_mutex_unlock (&backend->mutex);
    g_paste_history_saver_drain (saver);
    while (g_main_context_iteration (NULL, FALSE))
        ;
}

static void
record (GPasteHistorySaver *saver,
        const gchar        *name,
        const gchar        *head)
{
    g_paste_history_saver_record (saver, G_PASTE_HISTORY_SAVE_FULL, name, NULL, NULL, g_list_prepend (NULL, g_paste_text_item_new (head)));
}

/* A store rewritten whole coalesces its writes, but each history's on its
 * own: a switch leaves the history it left a snapshot nothing else would
 * write, and a write of the history switched to must not take its place. */
static void
test_history_names (void)
{
    g_autoptr (GObject) owner = g_object_new (G_TYPE_OBJECT, NULL);
    g_autoptr (TestBackend) backend = g_object_new (TEST_TYPE_BACKEND, NULL);
    g_autoptr (GPasteHistorySaver) saver = g_paste_history_saver_new (G_PASTE_STORAGE_BACKEND (backend), owner, on_loaded, on_diverged);

    record (saver, "A", "first");
    wait_for_worker (backend);
    record (saver, "A", "superseded");
    record (saver, "B", "other");
    record (saver, "A", "latest");
    finish_worker (backend, saver);

    g_assert_cmpuint (backend->writes, ==, 3);
    g_assert_cmpstr (g_hash_table_lookup (backend->values, "A"), ==, "latest");
    g_assert_cmpstr (g_hash_table_lookup (backend->values, "B"), ==, "other");
}

/* A store applied one operation at a time reads another history only once
 * every operation queued for any history has landed: the SQLite flavours'
 * single cached connection would otherwise be taken from under them, and
 * opened again on each side -- a key derivation each on an encrypted store. */
static void
test_incremental_load_waits (void)
{
    g_autoptr (GObject) owner = g_object_new (G_TYPE_OBJECT, NULL);
    g_autoptr (TestIncremental) backend = g_object_new (TEST_TYPE_INCREMENTAL, NULL);
    g_autoptr (GPasteHistorySaver) saver = g_paste_history_saver_new (G_PASTE_STORAGE_BACKEND (backend), owner, on_loaded, on_diverged);
    g_autoptr (GPasteItem) added = g_paste_text_item_new ("added");
    gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

    g_assert_true (g_paste_storage_backend_is_incremental (G_PASTE_STORAGE_BACKEND (backend)));
    g_paste_history_saver_record (saver, G_PASTE_HISTORY_SAVE_ADD, "A", added, NULL, NULL);

    g_mutex_lock (&backend->mutex);
    while (!backend->started)
        g_assert_true (g_cond_wait_until (&backend->cond, &backend->mutex, deadline));
    g_mutex_unlock (&backend->mutex);

    g_paste_history_saver_load (saver, "B", FALSE);
    /* Long enough for a read started at once to have been counted. */
    g_usleep (100 * 1000);
    g_assert_cmpuint (backend->reads, ==, 0);

    g_mutex_lock (&backend->mutex);
    backend->released = TRUE;
    g_cond_broadcast (&backend->cond);
    g_mutex_unlock (&backend->mutex);

    while (g_paste_history_saver_is_loading (saver))
        g_main_context_iteration (NULL, TRUE);
    g_assert_cmpuint (backend->reads, ==, 1);
}

int
main (int    argc,
      char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/saver/history-names", test_history_names);
    g_test_add_func ("/saver/incremental-load-waits", test_incremental_load_waits);

    return g_paste_test_env_run ();
}
