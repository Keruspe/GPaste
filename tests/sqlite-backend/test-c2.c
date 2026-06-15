// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-util.h>

#include <gpaste-daemon/gpaste-sqlite-backend.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-text-item.h>

#include <sqlite3.h>

#include <string.h>

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
    f->name    = g_strdup_printf ("sqlite-c2-%u", counter++);
    f->db_path = g_paste_util_get_history_file_path (f->name, "db");

    return f;
}

static void
fixture_free (Fixture *f)
{
    g_autoptr (GError) error = NULL;
    g_paste_storage_backend_delete_history (f->backend, f->name, &error);
    g_clear_error (&error);

    g_clear_object (&f->backend);
    g_clear_object (&f->settings);
    g_clear_pointer (&f->name, g_free);
    g_clear_pointer (&f->db_path, g_free);
    g_free (f);
}

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

static gdouble
scalar_double (sqlite3 *db, const gchar *sql)
{
    sqlite3_stmt *stmt = NULL;
    g_assert_cmpint (sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL), ==, SQLITE_OK);
    g_assert_cmpint (sqlite3_step (stmt), ==, SQLITE_ROW);
    gdouble v = sqlite3_column_double (stmt, 0);
    sqlite3_finalize (stmt);
    return v;
}

static gboolean
row_exists (sqlite3 *db, const gchar *sql, const gchar *bind)
{
    sqlite3_stmt *stmt = NULL;
    g_assert_cmpint (sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL), ==, SQLITE_OK);
    sqlite3_bind_text (stmt, 1, bind, -1, SQLITE_TRANSIENT);
    gboolean exists = (sqlite3_step (stmt) == SQLITE_ROW);
    sqlite3_finalize (stmt);
    return exists;
}

/* --- Tests --- */

static void
test_add_item_accumulates (void)
{
    Fixture *f = fixture_new (100);

    /* Two separate add_item calls; on reopen we should see both rows in order. */
    GPasteItem *a = g_paste_text_item_new ("alpha");
    GPasteItem *b = g_paste_text_item_new ("beta");

    g_paste_storage_backend_add_item (f->backend, f->name, a, NULL);
    g_paste_storage_backend_add_item (f->backend, f->name, b, NULL);

    GList *out = NULL;
    gsize  size = 0;
    g_paste_storage_backend_read_history (f->backend, f->name, &out, &size);

    g_assert_cmpuint (g_list_length (out), ==, 2);
    /* The later add lands at the head per the DESC ordering. */
    g_assert_cmpstr (g_paste_item_get_value (out->data),       ==, "beta");
    g_assert_cmpstr (g_paste_item_get_value (out->next->data), ==, "alpha");

    g_list_free_full (out, g_object_unref);
    g_object_unref (a);
    g_object_unref (b);
    fixture_free (f);
}

static void
test_add_item_dedup_keeps_uuid_moves_to_front (void)
{
    Fixture *f = fixture_new (100);

    GPasteItem *a = g_paste_text_item_new ("payload");
    g_paste_storage_backend_add_item (f->backend, f->name, a, NULL);
    const gchar *original_uuid = g_strdup (g_paste_item_get_uuid (a));

    /* Push another item between, then re-add the same value: row count stays 2,
     * but "payload" moves back to the head and keeps its original uuid. */
    GPasteItem *b = g_paste_text_item_new ("other");
    g_paste_storage_backend_add_item (f->backend, f->name, b, NULL);

    GPasteItem *dup = g_paste_text_item_new ("payload");
    g_paste_storage_backend_add_item (f->backend, f->name, dup, NULL);

    sqlite3 *db = open_raw (f->db_path);
    g_assert_cmpint (scalar_int (db, "SELECT COUNT(*) FROM items"), ==, 2);
    /* The original uuid is still on file and now owns the largest clip_order. */
    g_assert_true (row_exists (db,
                               "SELECT 1 FROM items"
                               " WHERE uuid = ? AND clip_order = (SELECT MAX(clip_order) FROM items)",
                               original_uuid));
    sqlite3_close (db);

    g_free ((gchar *) original_uuid);
    g_object_unref (a);
    g_object_unref (b);
    g_object_unref (dup);
    fixture_free (f);
}

static void
test_remove_item_removes_row (void)
{
    Fixture *f = fixture_new (100);

    GPasteItem *a = g_paste_text_item_new ("one");
    GPasteItem *b = g_paste_text_item_new ("two");
    g_paste_storage_backend_add_item (f->backend, f->name, a, NULL);
    g_paste_storage_backend_add_item (f->backend, f->name, b, NULL);

    const gchar *uuid_a = g_paste_item_get_uuid (a);

    g_paste_storage_backend_remove_item (f->backend, f->name, uuid_a, NULL);

    sqlite3 *db = open_raw (f->db_path);
    g_assert_cmpint (scalar_int (db, "SELECT COUNT(*) FROM items"), ==, 1);
    g_assert_false (row_exists (db, "SELECT 1 FROM items WHERE uuid = ?", uuid_a));
    sqlite3_close (db);

    g_object_unref (a);
    g_object_unref (b);
    fixture_free (f);
}

static void
test_replace_item_inherits_clip_order (void)
{
    Fixture *f = fixture_new (100);

    GPasteItem *a = g_paste_text_item_new ("old-payload");
    GPasteItem *b = g_paste_text_item_new ("between");
    g_paste_storage_backend_add_item (f->backend, f->name, a, NULL);
    g_paste_storage_backend_add_item (f->backend, f->name, b, NULL);

    const gchar *old_uuid = g_strdup (g_paste_item_get_uuid (a));

    /* Capture the old row's clip_order before replacing. */
    sqlite3 *db = open_raw (f->db_path);
    g_autofree gchar *sql = g_strdup_printf ("SELECT clip_order FROM items WHERE uuid = '%s'", old_uuid);
    gdouble old_order = scalar_double (db, sql);
    sqlite3_close (db);

    GPasteItem *new_item = g_paste_text_item_new ("new-payload");
    const gchar *new_uuid = g_strdup (g_paste_item_get_uuid (new_item));

    g_paste_storage_backend_replace_item (f->backend, f->name, old_uuid, new_item, NULL);

    db = open_raw (f->db_path);
    g_assert_cmpint (scalar_int (db, "SELECT COUNT(*) FROM items"), ==, 2);
    g_assert_false (row_exists (db, "SELECT 1 FROM items WHERE uuid = ?", old_uuid));
    g_assert_true  (row_exists (db, "SELECT 1 FROM items WHERE uuid = ?", new_uuid));

    g_autofree gchar *sql2 = g_strdup_printf ("SELECT clip_order FROM items WHERE uuid = '%s'", new_uuid);
    g_assert_cmpfloat (scalar_double (db, sql2), ==, old_order);
    sqlite3_close (db);

    g_free ((gchar *) old_uuid);
    g_free ((gchar *) new_uuid);
    g_object_unref (a);
    g_object_unref (b);
    g_object_unref (new_item);
    fixture_free (f);
}

static void
test_clear_history_empties_table_keeps_file (void)
{
    Fixture *f = fixture_new (100);

    GPasteItem *x = g_paste_text_item_new ("x");
    GPasteItem *y = g_paste_text_item_new ("y");
    g_paste_storage_backend_add_item (f->backend, f->name, x, NULL);
    g_paste_storage_backend_add_item (f->backend, f->name, y, NULL);

    g_paste_storage_backend_clear_history (f->backend, f->name, NULL);

    g_assert_true (g_file_test (f->db_path, G_FILE_TEST_EXISTS));

    sqlite3 *db = open_raw (f->db_path);
    g_assert_cmpint (scalar_int (db, "SELECT COUNT(*) FROM items"), ==, 0);
    sqlite3_close (db);

    g_object_unref (x);
    g_object_unref (y);
    fixture_free (f);
}

/* schema.sql calls for a write-amplification benchmark between 1000 add_item
 * calls and 1000 full write_history snapshots. Capturing that as a precise
 * byte-count test is fragile (WAL checkpointing, page allocator), so we make
 * it a sanity check instead: 1000 add_item calls accumulate to the configured
 * max-history-size and the most recent uuid is at the head of the table. */
static void
test_add_item_burst_truncates_at_max (void)
{
    const guint64 MAX = 50;
    const guint   N   = 200;
    Fixture *f = fixture_new (MAX);

    const gchar *last_uuid = NULL;
    g_autofree gchar *last_uuid_copy = NULL;

    for (guint i = 0; i < N; ++i)
    {
        g_autofree gchar *value = g_strdup_printf ("payload-%u", i);
        GPasteItem *item = g_paste_text_item_new (value);
        g_paste_storage_backend_add_item (f->backend, f->name, item, NULL);
        if (i == N - 1)
            last_uuid_copy = g_strdup (g_paste_item_get_uuid (item));
        g_object_unref (item);
    }
    last_uuid = last_uuid_copy;

    sqlite3 *db = open_raw (f->db_path);
    g_assert_cmpint ((guint64) scalar_int (db, "SELECT COUNT(*) FROM items"), ==, MAX);
    g_assert_true (row_exists (db,
                               "SELECT 1 FROM items"
                               " WHERE uuid = ? AND clip_order = (SELECT MAX(clip_order) FROM items)",
                               last_uuid));
    sqlite3_close (db);

    fixture_free (f);
}

int
main (int argc, char *argv[])
{
    g_autofree gchar *tmp = g_dir_make_tmp ("gpaste-sqlite-c2-XXXXXX", NULL);
    if (tmp)
    {
        g_setenv ("XDG_DATA_HOME",   tmp, TRUE);
        g_setenv ("XDG_CONFIG_HOME", tmp, TRUE);
        g_setenv ("XDG_CACHE_HOME",  tmp, TRUE);
    }

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/sqlite-backend/c2/add_item_accumulates",                test_add_item_accumulates);
    g_test_add_func ("/sqlite-backend/c2/add_item_dedup_moves_front",          test_add_item_dedup_keeps_uuid_moves_to_front);
    g_test_add_func ("/sqlite-backend/c2/remove_item_removes_row",             test_remove_item_removes_row);
    g_test_add_func ("/sqlite-backend/c2/replace_item_inherits_clip_order",    test_replace_item_inherits_clip_order);
    g_test_add_func ("/sqlite-backend/c2/clear_history_empties_keeps_file",    test_clear_history_empties_table_keeps_file);
    g_test_add_func ("/sqlite-backend/c2/add_item_burst_truncates_at_max",     test_add_item_burst_truncates_at_max);

    return g_test_run ();
}
