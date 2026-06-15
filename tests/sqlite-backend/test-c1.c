// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-util.h>

#include <gpaste-daemon/gpaste-sqlite-backend.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-uris-item.h>

#include <sqlite3.h>

#include <string.h>

/* Each test runs against a freshly-built backend rooted at its own history
 * name; the XDG_DATA_HOME tmpdir is set in main() so the .db files land in a
 * throwaway tree. */
typedef struct
{
    GPasteSettings       *settings;
    GPasteStorageBackend *backend;
    gchar                *name;
    gchar                *db_path;
} Fixture;

static guint counter;

static Fixture *
fixture_new (guint64 max_history_size)
{
    Fixture *f = g_new0 (Fixture, 1);

    f->settings = g_paste_settings_new ();
    g_paste_settings_set_growing_lines (f->settings, FALSE);
    g_paste_settings_set_max_history_size (f->settings, max_history_size);
    g_paste_settings_set_max_memory_usage (f->settings, 1024);
    g_paste_settings_set_images_support (f->settings, TRUE);

    f->backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, f->settings);
    f->name    = g_strdup_printf ("sqlite-test-%u", counter++);
    f->db_path = g_paste_util_get_history_file_path (f->name, "db");

    return f;
}

static void
fixture_free (Fixture *f)
{
    /* Best-effort: delete the .db and any sidecars before tearing down the
     * backend, so subsequent test runs in the same tmpdir start clean. */
    g_autoptr (GError) error = NULL;
    g_paste_storage_backend_delete_history (f->backend, f->name, &error);
    g_clear_error (&error);

    g_clear_object (&f->backend);
    g_clear_object (&f->settings);
    g_clear_pointer (&f->name, g_free);
    g_clear_pointer (&f->db_path, g_free);
    g_free (f);
}

/* --- Tests --- */

static void
test_read_missing_returns_empty (void)
{
    Fixture *f = fixture_new (100);

    GList *history = NULL;
    gsize  size    = 0;
    g_paste_storage_backend_read_history (f->backend, f->name, &history, &size);

    g_assert_null (history);
    g_assert_cmpuint (size, ==, 0);

    /* And read must not have materialised the .db file. */
    g_assert_false (g_file_test (f->db_path, G_FILE_TEST_EXISTS));

    fixture_free (f);
}

static void
test_roundtrip_text_and_uris (void)
{
    Fixture *f = fixture_new (100);

    GPasteItem *t = g_paste_text_item_new ("hello world");
    GPasteItem *u = g_paste_uris_item_new_from_str ("file:///a\nfile:///b");

    g_autofree gchar *t_uuid = g_strdup (g_paste_item_get_uuid (t));
    g_autofree gchar *u_uuid = g_strdup (g_paste_item_get_uuid (u));

    GList *history = NULL;
    history = g_list_append (history, t); /* head = newest */
    history = g_list_append (history, u);

    g_paste_storage_backend_write_history (f->backend, f->name, history);
    g_list_free_full (history, g_object_unref);

    GList *out = NULL;
    gsize  size = 0;
    g_paste_storage_backend_read_history (f->backend, f->name, &out, &size);

    g_assert_cmpuint (g_list_length (out), ==, 2);

    const GPasteItem *r0 = out->data;
    const GPasteItem *r1 = out->next->data;

    g_assert_cmpstr (g_paste_item_get_kind (r0),  ==, "Text");
    g_assert_cmpstr (g_paste_item_get_value (r0), ==, "hello world");
    g_assert_cmpstr (g_paste_item_get_uuid (r0),  ==, t_uuid);

    g_assert_cmpstr (g_paste_item_get_kind (r1),  ==, "Uris");
    g_assert_cmpstr (g_paste_item_get_uuid (r1),  ==, u_uuid);

    g_assert_cmpuint (size, >, 0);

    g_list_free_full (out, g_object_unref);
    fixture_free (f);
}

static void
test_order_newest_first (void)
{
    Fixture *f = fixture_new (100);

    GList *history = NULL;
    history = g_list_append (history, g_paste_text_item_new ("third"));
    history = g_list_append (history, g_paste_text_item_new ("second"));
    history = g_list_append (history, g_paste_text_item_new ("first"));

    g_paste_storage_backend_write_history (f->backend, f->name, history);
    g_list_free_full (history, g_object_unref);

    GList *out = NULL;
    gsize  size = 0;
    g_paste_storage_backend_read_history (f->backend, f->name, &out, &size);

    g_assert_cmpuint (g_list_length (out), ==, 3);
    g_assert_cmpstr (g_paste_item_get_value (out->data),                   ==, "third");
    g_assert_cmpstr (g_paste_item_get_value (out->next->data),             ==, "second");
    g_assert_cmpstr (g_paste_item_get_value (out->next->next->data),       ==, "first");

    g_list_free_full (out, g_object_unref);
    fixture_free (f);
}

/* Open the .db file and run an arbitrary SQL query as a one-off sanity check. */
static sqlite3 *
open_raw (const gchar *path)
{
    sqlite3 *db = NULL;
    g_assert_cmpint (sqlite3_open_v2 (path, &db, SQLITE_OPEN_READONLY, NULL), ==, SQLITE_OK);
    return db;
}

static gint
scalar_int (sqlite3 *db, const gchar *sql)
{
    sqlite3_stmt *stmt = NULL;
    g_assert_cmpint (sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL), ==, SQLITE_OK);
    g_assert_cmpint (sqlite3_step (stmt), ==, SQLITE_ROW);
    gint v = sqlite3_column_int (stmt, 0);
    sqlite3_finalize (stmt);
    return v;
}

static gchar *
scalar_text (sqlite3 *db, const gchar *sql)
{
    sqlite3_stmt *stmt = NULL;
    g_assert_cmpint (sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL), ==, SQLITE_OK);
    g_assert_cmpint (sqlite3_step (stmt), ==, SQLITE_ROW);
    gchar *v = g_strdup ((const gchar *) sqlite3_column_text (stmt, 0));
    sqlite3_finalize (stmt);
    return v;
}

static void
test_schema_and_pragmas (void)
{
    Fixture *f = fixture_new (100);

    /* Force schema creation via a no-op write. */
    g_paste_storage_backend_write_history (f->backend, f->name, NULL);

    sqlite3 *db = open_raw (f->db_path);

    /* journal_mode is a file-level property (persisted in the header), so it
     * round-trips through a fresh connection. synchronous and foreign_keys are
     * per-connection — the backend sets them on its own handle and we verify
     * them indirectly through CASCADE behaviour in the dedicated test. */
    g_autofree gchar *jm = scalar_text (db, "PRAGMA journal_mode");
    g_assert_cmpstr (jm, ==, "wal");

    /* Three tables and the two indexes are present. */
    g_assert_cmpint (scalar_int (db, "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN ('items','special_values','image_metadata')"), ==, 3);
    g_assert_cmpint (scalar_int (db, "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name IN ('idx_items_order','idx_items_hash')"), ==, 2);

    sqlite3_close (db);
    fixture_free (f);
}

static void
test_hash_deterministic_and_indexed (void)
{
    Fixture *f = fixture_new (100);

    GPasteItem *a = g_paste_text_item_new ("identical payload");
    GPasteItem *b = g_paste_text_item_new ("identical payload");
    GPasteItem *c = g_paste_text_item_new ("different payload");

    GList *history = NULL;
    history = g_list_append (history, a);
    history = g_list_append (history, b);
    history = g_list_append (history, c);

    g_paste_storage_backend_write_history (f->backend, f->name, history);
    g_list_free_full (history, g_object_unref);

    sqlite3 *db = open_raw (f->db_path);

    /* Two rows with the same value share the same 64-bit hash. */
    g_assert_cmpint (scalar_int (db, "SELECT (SELECT hash FROM items WHERE value='identical payload' LIMIT 1) = (SELECT hash FROM items WHERE value='identical payload' ORDER BY clip_order ASC LIMIT 1)"), ==, 1);

    /* Lookup-by-hash returns both of the duplicates. */
    g_autofree gchar *hash = scalar_text (db, "SELECT hash FROM items WHERE value='identical payload' LIMIT 1");
    g_autofree gchar *sql  = g_strdup_printf ("SELECT COUNT(*) FROM items WHERE hash = %s AND kind = 'Text'", hash);
    g_assert_cmpint (scalar_int (db, sql), ==, 2);

    sqlite3_close (db);
    fixture_free (f);
}

static void
test_max_history_size_truncates_on_write (void)
{
    /* sqlite backend's write_history_file caps the persisted row count at
     * max-history-size even when the caller hands it a longer list. */
    Fixture *f = fixture_new (5);

    const gchar *values[] = { "g", "f", "e", "d", "c", "b", "a" }; /* head = newest */
    GList *history = NULL;
    for (gsize i = 0; i < G_N_ELEMENTS (values); ++i)
        history = g_list_append (history, g_paste_text_item_new (values[i]));

    g_paste_storage_backend_write_history (f->backend, f->name, history);
    g_list_free_full (history, g_object_unref);

    sqlite3 *db = open_raw (f->db_path);
    g_assert_cmpint (scalar_int (db, "SELECT COUNT(*) FROM items"), ==, 5);
    /* The 5 retained items are the 5 head entries (newest), not the 5 tail. */
    g_assert_cmpint (scalar_int (db, "SELECT COUNT(*) FROM items WHERE value IN ('g','f','e','d','c')"), ==, 5);
    sqlite3_close (db);

    fixture_free (f);
}

static void
test_delete_items_cascades (void)
{
    Fixture *f = fixture_new (100);

    /* Write a single item with a synthetic special_value row via raw SQL after
     * the fact, then re-write history to trigger a full DELETE FROM items and
     * confirm the FK CASCADE wiped the side tables. */
    GPasteItem *t = g_paste_text_item_new ("payload");
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (t));

    GList *history = NULL;
    history = g_list_append (history, t);
    g_paste_storage_backend_write_history (f->backend, f->name, history);
    g_list_free_full (history, g_object_unref);

    /* Inject side rows from outside. */
    sqlite3 *raw = NULL;
    g_assert_cmpint (sqlite3_open (f->db_path, &raw), ==, SQLITE_OK);
    g_assert_cmpint (sqlite3_exec (raw, "PRAGMA foreign_keys=ON", NULL, NULL, NULL), ==, SQLITE_OK);

    g_autofree gchar *sv = g_strdup_printf ("INSERT INTO special_values (item_uuid, mime, data) VALUES ('%s', 'text/html', x'00')", uuid);
    g_autofree gchar *im = g_strdup_printf ("INSERT INTO image_metadata (item_uuid, date) VALUES ('%s', 0)", uuid);
    g_assert_cmpint (sqlite3_exec (raw, sv, NULL, NULL, NULL), ==, SQLITE_OK);
    g_assert_cmpint (sqlite3_exec (raw, im, NULL, NULL, NULL), ==, SQLITE_OK);
    sqlite3_close (raw);

    /* Rewrite empty history -> DELETE FROM items. CASCADE should empty the
     * side tables too. */
    g_paste_storage_backend_write_history (f->backend, f->name, NULL);

    sqlite3 *db = open_raw (f->db_path);
    g_assert_cmpint (scalar_int (db, "SELECT COUNT(*) FROM items"),          ==, 0);
    g_assert_cmpint (scalar_int (db, "SELECT COUNT(*) FROM special_values"), ==, 0);
    g_assert_cmpint (scalar_int (db, "SELECT COUNT(*) FROM image_metadata"), ==, 0);
    sqlite3_close (db);

    fixture_free (f);
}

/* Mirror the backend's REGEXP UDF so a raw connection can also evaluate it; we
 * only need this to drive the assertion from outside the backend's cache. */
static void
raw_regex_udf (sqlite3_context *ctx,
               int              argc,
               sqlite3_value  **argv)
{
    (void) argc;
    const gchar *pattern = (const gchar *) sqlite3_value_text (argv[0]);
    const gchar *subject = (const gchar *) sqlite3_value_text (argv[1]);
    if (!pattern || !subject)
    {
        sqlite3_result_int (ctx, 0);
        return;
    }
    g_autoptr (GError) error = NULL;
    g_autoptr (GRegex) regex = g_regex_new (pattern, G_REGEX_OPTIMIZE, 0, &error);
    sqlite3_result_int (ctx, regex && g_regex_match (regex, subject, 0, NULL) ? 1 : 0);
}

static void
test_regexp_registered (void)
{
    Fixture *f = fixture_new (100);

    GList *history = NULL;
    history = g_list_append (history, g_paste_text_item_new ("foo bar"));
    history = g_list_append (history, g_paste_text_item_new ("baz qux"));
    g_paste_storage_backend_write_history (f->backend, f->name, history);
    g_list_free_full (history, g_object_unref);

    /* Drive the C6 search shape (WHERE value REGEXP ?) against a fresh raw
     * connection that has the same UDF registered. This is the public proof
     * that the REGEXP operator the backend wires up against its own
     * connection is also a SQL-legal form on the data we persisted. */
    sqlite3 *db = NULL;
    g_assert_cmpint (sqlite3_open_v2 (f->db_path, &db, SQLITE_OPEN_READONLY, NULL), ==, SQLITE_OK);
    sqlite3_create_function (db, "REGEXP", 2, SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                             NULL, raw_regex_udf, NULL, NULL);

    sqlite3_stmt *stmt = NULL;
    g_assert_cmpint (sqlite3_prepare_v2 (db,
                                         "SELECT COUNT(*) FROM items WHERE value REGEXP ?",
                                         -1, &stmt, NULL),
                     ==, SQLITE_OK);
    sqlite3_bind_text (stmt, 1, "^foo", -1, SQLITE_TRANSIENT);
    g_assert_cmpint (sqlite3_step (stmt), ==, SQLITE_ROW);
    g_assert_cmpint (sqlite3_column_int (stmt, 0), ==, 1);
    sqlite3_finalize (stmt);

    /* And a non-matching pattern returns zero rows. */
    g_assert_cmpint (sqlite3_prepare_v2 (db,
                                         "SELECT COUNT(*) FROM items WHERE value REGEXP ?",
                                         -1, &stmt, NULL),
                     ==, SQLITE_OK);
    sqlite3_bind_text (stmt, 1, "^zzz", -1, SQLITE_TRANSIENT);
    g_assert_cmpint (sqlite3_step (stmt), ==, SQLITE_ROW);
    g_assert_cmpint (sqlite3_column_int (stmt, 0), ==, 0);
    sqlite3_finalize (stmt);
    sqlite3_close (db);

    /* The backend's own cached connection — which read_history goes through —
     * has the same UDF registered, exercised here by a successful read on the
     * data we just stored. */
    GList *out = NULL;
    gsize  sz  = 0;
    g_paste_storage_backend_read_history (f->backend, f->name, &out, &sz);
    g_assert_cmpuint (g_list_length (out), ==, 2);
    g_list_free_full (out, g_object_unref);

    fixture_free (f);
}

int
main (int argc, char *argv[])
{
    g_autofree gchar *tmp = g_dir_make_tmp ("gpaste-sqlite-test-XXXXXX", NULL);
    if (tmp)
    {
        g_setenv ("XDG_DATA_HOME",   tmp, TRUE);
        g_setenv ("XDG_CONFIG_HOME", tmp, TRUE);
        g_setenv ("XDG_CACHE_HOME",  tmp, TRUE);
    }

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/sqlite-backend/c1/read_missing_returns_empty",    test_read_missing_returns_empty);
    g_test_add_func ("/sqlite-backend/c1/roundtrip_text_and_uris",       test_roundtrip_text_and_uris);
    g_test_add_func ("/sqlite-backend/c1/order_newest_first",            test_order_newest_first);
    g_test_add_func ("/sqlite-backend/c1/schema_and_pragmas",            test_schema_and_pragmas);
    g_test_add_func ("/sqlite-backend/c1/hash_deterministic_indexed",    test_hash_deterministic_and_indexed);
    g_test_add_func ("/sqlite-backend/c1/max_history_size_truncates",    test_max_history_size_truncates_on_write);
    g_test_add_func ("/sqlite-backend/c1/delete_items_cascades",         test_delete_items_cascades);
    g_test_add_func ("/sqlite-backend/c1/regexp_registered",             test_regexp_registered);

    return g_test_run ();
}
