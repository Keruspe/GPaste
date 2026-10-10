// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>
#include <gpaste-test-env.h>
#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-storage-migration.h>
#include <gpaste-daemon/gpaste-text-item.h>

#include <dlfcn.h>
#include <string.h>

#ifdef G_PASTE_ENABLE_SQLITE
#include <gpaste-daemon/gpaste-sqlite-backend.h>

#include <sqlite3.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC (sqlite3, sqlite3_close)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (sqlite3_stmt, sqlite3_finalize)
#endif

/* Writes through a backend of the first kind leave the history's first item
 * out, as a destination that took a write and does not give it back would;
 * file writes through one of the second kind lose the document's footer, as an
 * interrupted one would. -1 for none. */
static gint shortened_kind = -1;
static gint truncated_kind = -1;

void
g_paste_storage_backend_write_history (GPasteStorageBackend *backend,
                                       const gchar          *name,
                                       const GList          *items)
{
    typedef void (*Write) (GPasteStorageBackend *, const gchar *, const GList *);
    Write original = (Write) dlsym (RTLD_NEXT, "g_paste_storage_backend_write_history");
    gint kind = g_paste_storage_backend_get_kind (backend);

    g_assert_nonnull (original);
    if (items && kind == shortened_kind)
        items = items->next;
    original (backend, name, items);

    if (kind != truncated_kind)
        return;

    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (backend, name);
    g_autofree gchar *contents = NULL;

    g_assert_true (g_file_get_contents (path, &contents, NULL, NULL));

    const gchar *footer = strstr (contents, "</history>");

    g_assert_nonnull (footer);
    g_assert_true (g_file_set_contents (path, contents, footer - contents, NULL));
}

/* The offered backends less this kind, standing for a build without the
 * feature it needs: -1 for none. The library asks through its own exported
 * symbol, which this one takes the place of. */
static gint hidden_kind = -1;

const GPasteStorage *
g_paste_prompt_list_storage_backends (guint *n_offered)
{
    typedef const GPasteStorage *(*List) (guint *);
    List original = (List) dlsym (RTLD_NEXT, "g_paste_prompt_list_storage_backends");
    static GPasteStorage kept[G_PASTE_N_STORAGE];

    g_assert_nonnull (original);

    const GPasteStorage *offered = original (n_offered);
    guint n_kept = 0;

    if (hidden_kind < 0)
        return offered;

    for (guint i = 0; i < *n_offered; ++i)
    {
        if ((gint) offered[i] != hidden_kind)
            kept[n_kept++] = offered[i];
    }

    *n_offered = n_kept;

    return kept;
}

/* The migration's own static helpers, detect_current_backend() and
 * import_histories() among them, are what these cases drive. */
#include "../../src/libgpaste/gpaste-daemon/gpaste-storage-migration.c"

/* A migration prompt answered at once with what the case put in it, counting
 * what it is told afterwards. */
#define TEST_TYPE_MIGRATION_PROMPT (test_migration_prompt_get_type ())
G_DECLARE_FINAL_TYPE (TestMigrationPrompt, test_migration_prompt, TEST, MIGRATION_PROMPT, GObject)

struct _TestMigrationPrompt
{
    GObject parent_instance;

    gboolean      dismiss;
    GPasteStorage chosen;
    gboolean      import;
    gboolean      cleanup;
    guint         reports;
};

static void test_migration_prompt_iface_init (GPastePromptInterface *iface);

G_DEFINE_TYPE_WITH_CODE (TestMigrationPrompt, test_migration_prompt, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_PASTE_TYPE_PROMPT, test_migration_prompt_iface_init))

static void
test_migration_prompt_migration (GPastePrompt        *prompt,
                                 GPastePromptRequest *request)
{
    TestMigrationPrompt *self = TEST_MIGRATION_PROMPT (prompt);

    if (self->dismiss)
        g_paste_prompt_request_dismiss (request);
    else
        g_paste_prompt_request_reply_migration (request, self->chosen, self->import, self->cleanup);
}

static void
test_migration_prompt_report (GPastePrompt *prompt,
                              const gchar  *title   G_GNUC_UNUSED,
                              const gchar  *message G_GNUC_UNUSED)
{
    ++TEST_MIGRATION_PROMPT (prompt)->reports;
}

static void
test_migration_prompt_iface_init (GPastePromptInterface *iface)
{
    iface->migration = test_migration_prompt_migration;
    iface->report = test_migration_prompt_report;
}

static void
test_migration_prompt_class_init (TestMigrationPromptClass *klass G_GNUC_UNUSED)
{
}

static void
test_migration_prompt_init (TestMigrationPrompt *self G_GNUC_UNUSED)
{
}

static void
on_migration_settled (GObject      *source G_GNUC_UNUSED,
                      GAsyncResult *result,
                      gpointer      user_data)
{
    g_set_object ((GAsyncResult **) user_data, result);
}

/* Run the migration through @prompt and hand back whether it settled, with
 * @error set when it did not. */
static gboolean
run_migration (GPasteSettings      *settings,
               TestMigrationPrompt *prompt,
               GError             **error)
{
    g_autoptr (GAsyncResult) result = NULL;

    g_paste_storage_migration_async (G_PASTE_PROMPT (prompt), settings, on_migration_settled, &result);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &result, NULL);
    g_assert_nonnull (result);

    return g_paste_storage_migration_finish (result, error);
}

/* A history in a format this build cannot open is still recognised as that
 * format: a dismissed migration leaves storage-backend naming it and the file
 * where it is, for a build that can open it. With every feature built, this
 * holds whatever detection lists; it is the builds lacking one that it is
 * about. */
static void
test_migration_keeps_unopenable_identity (gconstpointer data)
{
    GPasteStorage kind = GPOINTER_TO_UINT (data);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    guint64 revision = G_PASTE_STORAGE_BACKEND_REVISION - 1;
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (kind));

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);
    g_paste_settings_set_storage_backend_revision (settings, revision);
    g_assert_true (g_paste_util_ensure_history_dir_exists ());
    g_assert_true (g_file_set_contents (path, "not read by anything here", -1, NULL));

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GError) error = NULL;

    prompt->dismiss = TRUE;
    g_assert_true (run_migration (settings, prompt, &error));
    g_assert_no_error (error);

    g_assert_cmpint (g_paste_settings_get_storage_backend (settings), ==, kind);
    g_assert_cmpuint (g_paste_settings_get_storage_backend_revision (settings), ==, revision);
    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* An encrypted flavour this build cannot open asks for no passphrase, which
 * nothing could verify, and its backend says why it stores nothing. */
static void
test_unlock_skips_unopenable_flavour (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*This build cannot open the encrypted storage backend*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    hidden_kind = G_PASTE_STORAGE_ENCRYPTED_FILE;
    g_assert_false (g_paste_storage_decryption_needed (settings));

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_ENCRYPTED_FILE, settings);

    hidden_kind = -1;
    g_assert_cmpint (g_paste_storage_backend_get_kind (backend), ==, G_PASTE_STORAGE_NOOP);
}
#endif

#ifdef G_PASTE_ENABLE_SQLITE
/* A reply choosing a storage it was not offered -- one this build cannot open,
 * a value out of range -- answers nothing, as a dismissal does: the backend
 * standing in for it would import over the files the cleanup deletes. The
 * request it is answered through refuses it, before the migration sees it. */
static void
test_migration_refuses_unoffered_choice (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*was not offered; dismissing it*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    const GPasteStorage unoffered[] = { G_PASTE_STORAGE_SQLITE, G_PASTE_N_STORAGE + 1 };

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);
    g_paste_settings_set_storage_backend_revision (settings, G_PASTE_STORAGE_BACKEND_REVISION - 1);

    g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("kept"));

    g_paste_storage_backend_write_history (file, name, items);

    for (guint i = 0; i < G_N_ELEMENTS (unoffered); ++i)
    {
        g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
        g_autoptr (GError) error = NULL;
        g_autolist (GPasteItem) kept = NULL;
        gsize size;

        hidden_kind = G_PASTE_STORAGE_SQLITE;
        prompt->chosen = unoffered[i];
        prompt->import = TRUE;
        prompt->cleanup = TRUE;
        g_assert_true (run_migration (settings, prompt, &error));
        g_assert_no_error (error);
        hidden_kind = -1;

        g_assert_cmpint (g_paste_settings_get_storage_backend (settings), ==, G_PASTE_STORAGE_FILE);
        g_assert_cmpuint (g_paste_settings_get_storage_backend_revision (settings), ==, G_PASTE_STORAGE_BACKEND_REVISION - 1);
        g_assert_true (g_paste_storage_backend_read_history (file, name, NULL, &kept, &size));
        g_assert_cmpuint (g_list_length (kept), ==, 1);
    }
}

/* Neither toggle is offered for a source this build cannot open, and a reply
 * ticking them anyway is clamped: reading the source through the backend that
 * stands in for it would import nothing, and deleting through it would delete
 * another flavour's histories. */
static void
test_migration_clamps_unopenable_source (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_SQLITE);
    g_paste_settings_set_storage_backend_revision (settings, G_PASTE_STORAGE_BACKEND_REVISION - 1);

    g_autoptr (GPasteStorageBackend) database = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("kept"));

    g_paste_storage_backend_write_history (database, name, items);

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GError) error = NULL;

    hidden_kind = G_PASTE_STORAGE_SQLITE;
    g_assert_false (g_paste_prompt_can_import (G_PASTE_STORAGE_SQLITE, G_PASTE_STORAGE_FILE));
    g_assert_false (g_paste_prompt_backend_changes (G_PASTE_STORAGE_SQLITE, G_PASTE_STORAGE_FILE));

    prompt->chosen = G_PASTE_STORAGE_FILE;
    prompt->import = TRUE;
    prompt->cleanup = TRUE;
    g_assert_true (run_migration (settings, prompt, &error));
    g_assert_no_error (error);
    hidden_kind = -1;

    g_assert_cmpint (g_paste_settings_get_storage_backend (settings), ==, G_PASTE_STORAGE_FILE);

    g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autolist (GPasteItem) imported = NULL;
    g_autolist (GPasteItem) kept = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (file, name, NULL, &imported, &size));
    g_assert_null (imported);
    g_assert_true (g_paste_storage_backend_read_history (database, name, NULL, &kept, &size));
    g_assert_cmpuint (g_list_length (kept), ==, 1);
}

/* An import that stops after writing to the destination takes that copy back
 * and says so: the next start would detect the database before the file, and
 * read the copy instead of the history the session went on writing. */
static void
test_import_failure_leaves_no_copy (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*did not read back as it was written*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    guint64 revision = G_PASTE_STORAGE_BACKEND_REVISION - 1;

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);
    g_paste_settings_set_storage_backend_revision (settings, revision);

    g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autolist (GPasteItem) items = NULL;

    items = g_list_append (items, g_paste_text_item_new ("newest"));
    items = g_list_append (items, g_paste_text_item_new ("oldest"));
    g_paste_storage_backend_write_history (file, name, items);

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GError) error = NULL;

    prompt->chosen = G_PASTE_STORAGE_SQLITE;
    prompt->import = TRUE;
    prompt->cleanup = TRUE;
    shortened_kind = G_PASTE_STORAGE_SQLITE;
    g_assert_true (run_migration (settings, prompt, &error));
    g_assert_no_error (error);
    shortened_kind = -1;

    g_assert_cmpuint (prompt->reports, ==, 1);
    g_assert_cmpint (g_paste_settings_get_storage_backend (settings), ==, G_PASTE_STORAGE_FILE);
    g_assert_cmpuint (g_paste_settings_get_storage_backend_revision (settings), ==, revision);

    g_autofree gchar *copy = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_SQLITE));
    g_autolist (GPasteItem) kept = NULL;
    gsize size;

    g_assert_false (g_file_test (copy, G_FILE_TEST_EXISTS));
    g_assert_true (g_paste_storage_backend_read_history (file, name, NULL, &kept, &size));
    g_assert_cmpuint (g_list_length (kept), ==, 2);

    GPasteStorage current = G_PASTE_STORAGE_NOOP;

    g_assert_true (detect_current_backend (settings, &current));
    g_assert_cmpint (current, ==, G_PASTE_STORAGE_FILE);
}

/* A destination that does not read back is no copy, however many of its items
 * the read got through before stopping: the source has to outlive it, even
 * with the cleanup asked for. */
static void
test_import_needs_readable_copy (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*could not be read back once written*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_SQLITE);
    g_paste_settings_set_storage_backend_revision (settings, G_PASTE_STORAGE_BACKEND_REVISION - 1);

    g_autoptr (GPasteStorageBackend) database = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("precious"));

    g_paste_storage_backend_write_history (database, name, items);

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GError) error = NULL;

    prompt->chosen = G_PASTE_STORAGE_FILE;
    prompt->import = TRUE;
    prompt->cleanup = TRUE;
    truncated_kind = G_PASTE_STORAGE_FILE;
    g_assert_true (run_migration (settings, prompt, &error));
    g_assert_no_error (error);
    truncated_kind = -1;

    g_assert_cmpuint (prompt->reports, ==, 1);
    g_assert_cmpint (g_paste_settings_get_storage_backend (settings), ==, G_PASTE_STORAGE_SQLITE);

    g_autolist (GPasteItem) kept = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (database, name, NULL, &kept, &size));
    g_assert_cmpuint (g_list_length (kept), ==, 1);
    g_assert_cmpstr (g_paste_item_get_value (kept->data), ==, "precious");

    /* And the copy that did not read back is taken back. */
    g_autofree gchar *copy = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_FILE));

    g_assert_false (g_file_test (copy, G_FILE_TEST_EXISTS));
}
#endif

#if defined(G_PASTE_ENABLE_ENCRYPTION) && defined(G_PASTE_ENABLE_SQLITE)
static gchar *
read_meta (sqlite3     *db,
           const gchar *key)
{
    g_autoptr (sqlite3_stmt) statement = NULL;

    g_assert_cmpint (sqlite3_prepare_v2 (db, "SELECT hex (value) FROM meta WHERE key = ?;", -1, &statement, NULL), ==, SQLITE_OK);
    g_assert_cmpint (sqlite3_bind_text (statement, 1, key, -1, SQLITE_TRANSIENT), ==, SQLITE_OK);
    g_assert_cmpint (sqlite3_step (statement), ==, SQLITE_ROW);

    return g_strdup ((const gchar *) sqlite3_column_text (statement, 0));
}

/* Encryption parameters with one missing are no new store when the history
 * holds data: its salt and key check have to survive the open that refuses
 * it, or nothing would ever decrypt that data again. */
static void
test_incomplete_crypto (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*incomplete encryption parameters*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "incomplete-crypto";

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, "passphrase");
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("precious"));

        g_paste_storage_backend_write_history (backend, name, items);
    }

    g_autofree gchar *path = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_ENCRYPTED_SQLITE));
    g_autoptr (sqlite3) db = NULL;

    g_assert_cmpint (sqlite3_open (path, &db), ==, SQLITE_OK);

    g_autofree gchar *salt = read_meta (db, "salt");
    g_autofree gchar *check = read_meta (db, "check");

    g_assert_cmpint (sqlite3_exec (db, "DELETE FROM meta WHERE key = 'memlimit';", NULL, NULL, NULL), ==, SQLITE_OK);
    g_clear_pointer (&db, sqlite3_close);

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, "passphrase");
        g_autolist (GPasteItem) items = NULL;
        gsize size;

        g_assert_false (g_paste_storage_backend_read_history (backend, name, NULL, &items, &size));
    }

    g_assert_cmpint (sqlite3_open (path, &db), ==, SQLITE_OK);

    g_autofree gchar *kept_salt = read_meta (db, "salt");
    g_autofree gchar *kept_check = read_meta (db, "check");

    g_assert_cmpstr (kept_salt, ==, salt);
    g_assert_cmpstr (kept_check, ==, check);
}
#endif

#if defined(G_PASTE_ENABLE_ENCRYPTION) && defined(G_PASTE_ENABLE_SQLITE)
#include <time.h>
#include <unistd.h>

/* The thread whose sleeps are a wait on a busy database, and whether it has
 * slept: SQLite's busy timeout waits between its attempts with nanosleep (), or
 * with usleep () in a build without it. */
static GThread *busy_sleeper;
static GMutex busy_mutex;
static GCond busy_cond;
static gboolean busy_slept;

static void
note_busy_sleep (void)
{
    if (!busy_sleeper || g_thread_self () != busy_sleeper)
        return;

    g_mutex_lock (&busy_mutex);
    busy_slept = TRUE;
    g_cond_broadcast (&busy_cond);
    g_mutex_unlock (&busy_mutex);
}

int
nanosleep (const struct timespec *duration,
           struct timespec       *remaining)
{
    typedef int (*Sleep) (const struct timespec *, struct timespec *);
    Sleep original = (Sleep) dlsym (RTLD_NEXT, "nanosleep");

    note_busy_sleep ();
    return original (duration, remaining);
}

int
usleep (useconds_t microseconds)
{
    typedef int (*Sleep) (useconds_t);
    Sleep original = (Sleep) dlsym (RTLD_NEXT, "usleep");

    note_busy_sleep ();
    return original (microseconds);
}

/* Lets the database go once its holder has been waited for -- or never was,
 * a few seconds on: by then a verification that did not wait has answered. */
static gpointer
close_once_waited_for (gpointer data)
{
    gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

    g_mutex_lock (&busy_mutex);
    while (!busy_slept && g_cond_wait_until (&busy_cond, &busy_mutex, deadline))
        ;
    g_mutex_unlock (&busy_mutex);
    sqlite3_close (data);

    return NULL;
}

/* Hold the database at @path busy from another connection, until the calling
 * thread has waited on it: the thread returned lets it go. */
static GThread *
hold_busy (const gchar *path)
{
    sqlite3 *holder = NULL;

    /* Exclusive locking mode keeps the lock past the transaction, shutting
     * every other connection out until this one closes. */
    g_assert_cmpint (sqlite3_open (path, &holder), ==, SQLITE_OK);
    g_assert_cmpint (sqlite3_exec (holder, "PRAGMA locking_mode = EXCLUSIVE; BEGIN EXCLUSIVE; COMMIT;", NULL, NULL, NULL), ==, SQLITE_OK);

    busy_slept = FALSE;
    busy_sleeper = g_thread_self ();

    return g_thread_new ("close-once-waited-for", close_once_waited_for, holder);
}

/* What hold_busy () held let go, the database having been waited for. */
static void
release_busy (GThread *closer)
{
    g_thread_join (closer);
    busy_sleeper = NULL;
    g_assert_true (busy_slept);
}

/* A database another connection is busy with is waited for, not read as one
 * holding nothing: a passphrase its key check refuses would otherwise pass
 * verification, as if the history were empty and up for keying afresh. */
static void
test_verification_waits_for_busy_database (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "busy-verification";

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, "passphrase");
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("precious"));

        g_paste_storage_backend_write_history (backend, name, items);
    }

    g_autofree gchar *path = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_ENCRYPTED_SQLITE));
    GThread *closer = hold_busy (path);

    g_assert_false (g_paste_storage_passphrase_can_decrypt (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings, "wrong passphrase"));
    release_busy (closer);
}
#endif

int
main (int    argc,
      char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

    g_test_add_data_func ("/storage/migration/encrypted-file-identity", GUINT_TO_POINTER (G_PASTE_STORAGE_ENCRYPTED_FILE), test_migration_keeps_unopenable_identity);
    g_test_add_data_func ("/storage/migration/sqlite-identity", GUINT_TO_POINTER (G_PASTE_STORAGE_SQLITE), test_migration_keeps_unopenable_identity);
    g_test_add_data_func ("/storage/migration/encrypted-sqlite-identity", GUINT_TO_POINTER (G_PASTE_STORAGE_ENCRYPTED_SQLITE), test_migration_keeps_unopenable_identity);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_test_add_func ("/storage/unlock/unopenable-flavour", test_unlock_skips_unopenable_flavour);
#endif
#if defined(G_PASTE_ENABLE_ENCRYPTION) && defined(G_PASTE_ENABLE_SQLITE)
    g_test_add_func ("/storage/sqlite/incomplete-crypto", test_incomplete_crypto);
    g_test_add_func ("/storage/sqlite/busy-verification", test_verification_waits_for_busy_database);
#endif
#ifdef G_PASTE_ENABLE_SQLITE
    g_test_add_func ("/storage/migration/unopenable-source", test_migration_clamps_unopenable_source);
    g_test_add_func ("/storage/migration/unoffered-choice", test_migration_refuses_unoffered_choice);
    g_test_add_func ("/storage/migration/import-failure", test_import_failure_leaves_no_copy);
    g_test_add_func ("/storage/migration/import-readability", test_import_needs_readable_copy);
#endif

    return g_paste_test_env_run ();
}
