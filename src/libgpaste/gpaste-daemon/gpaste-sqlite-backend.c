// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-util.h>

#include <gpaste-daemon/gpaste-color-item.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-special-atom.h>
#include <gpaste-daemon/gpaste-sqlite-backend.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-uris-item.h>

#include <sqlite3.h>

#include <string.h>

struct _GPasteSqliteBackend
{
    GPasteStorageBackend parent_instance;
};

typedef struct
{
    /* Cache of open sqlite3 connections keyed by absolute db file path. The
     * daemon keeps the backend alive for the whole process lifetime, so holding
     * the connections open (and the WAL hot) is the cheap path. Closed in dispose. */
    GHashTable *dbs;
} GPasteSqliteBackendPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (SqliteBackend, sqlite_backend, G_PASTE_TYPE_STORAGE_BACKEND)

#define SCHEMA_SQL                                                                                                     \
    "CREATE TABLE IF NOT EXISTS items ("                                                                               \
    "    uuid              TEXT    PRIMARY KEY,"                                                                       \
    "    kind              TEXT    NOT NULL,"                                                                          \
    "    value             TEXT    NOT NULL,"                                                                          \
    "    display_string    TEXT,"                                                                                      \
    "    hash              INTEGER,"                                                                                   \
    "    size              INTEGER NOT NULL DEFAULT 0,"                                                                \
    "    created_date      INTEGER NOT NULL,"                                                                          \
    "    last_paste_date   INTEGER,"                                                                                   \
    "    pinned            INTEGER NOT NULL DEFAULT 0,"                                                                \
    "    clip_order        REAL    NOT NULL DEFAULT 0,"                                                                \
    "    sticky_clip_order REAL"                                                                                       \
    ");"                                                                                                               \
    "CREATE INDEX IF NOT EXISTS idx_items_order ON items (sticky_clip_order DESC, clip_order DESC);"                   \
    "CREATE INDEX IF NOT EXISTS idx_items_hash  ON items (hash);"                                                      \
    "CREATE TABLE IF NOT EXISTS special_values ("                                                                      \
    "    item_uuid TEXT NOT NULL,"                                                                                     \
    "    mime      TEXT NOT NULL,"                                                                                     \
    "    data      BLOB NOT NULL,"                                                                                     \
    "    PRIMARY KEY (item_uuid, mime),"                                                                               \
    "    FOREIGN KEY (item_uuid) REFERENCES items(uuid) ON DELETE CASCADE"                                             \
    ");"                                                                                                               \
    "CREATE TABLE IF NOT EXISTS image_metadata ("                                                                      \
    "    item_uuid TEXT    PRIMARY KEY,"                                                                               \
    "    date      INTEGER NOT NULL,"                                                                                  \
    "    FOREIGN KEY (item_uuid) REFERENCES items(uuid) ON DELETE CASCADE"                                             \
    ");"

/* SHA256(value), take the first 16 hex chars as an unsigned 64-bit integer. The
 * column is INTEGER so we cast to gint64 unchanged (two's complement). */
static gint64
_g_paste_sqlite_backend_hash_u64 (const gchar *value)
{
    if (!value)
        return 0;

    g_autofree gchar *hex = g_compute_checksum_for_data (G_CHECKSUM_SHA256,
                                                         (const guchar *) value,
                                                         strlen (value));
    if (!hex || strlen (hex) < 16)
        return 0;

    gchar buf[17];
    memcpy (buf, hex, 16);
    buf[16] = '\0';

    return (gint64) g_ascii_strtoull (buf, NULL, 16);
}

static GPasteSpecialAtom
_g_paste_sqlite_backend_atom_from_mime (const gchar *mime)
{
    if (!mime)
        return G_PASTE_SPECIAL_ATOM_INVALID;

    for (GPasteSpecialAtom a = G_PASTE_SPECIAL_ATOM_FIRST; a < G_PASTE_SPECIAL_ATOM_LAST; ++a)
    {
        const gchar *s = g_paste_special_atom_get (a);
        if (s && g_paste_str_equal (s, mime))
            return a;
    }

    return G_PASTE_SPECIAL_ATOM_INVALID;
}

/* REGEXP(pattern, subject) -> 0/1, called by sqlite3 during scans. */
static void
_g_paste_sqlite_backend_regex_udf (sqlite3_context *ctx,
                                   int              argc G_GNUC_UNUSED,
                                   sqlite3_value  **argv)
{
    const gchar *pattern = (const gchar *) sqlite3_value_text (argv[0]);
    const gchar *subject = (const gchar *) sqlite3_value_text (argv[1]);

    if (!pattern || !subject)
    {
        sqlite3_result_int (ctx, 0);
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GRegex) regex = g_regex_new (pattern, G_REGEX_OPTIMIZE, 0, &error);
    if (!regex)
    {
        sqlite3_result_error (ctx, error->message, -1);
        return;
    }

    sqlite3_result_int (ctx, g_regex_match (regex, subject, 0, NULL) ? 1 : 0);
}

static gboolean
_g_paste_sqlite_backend_exec (sqlite3     *db,
                              const gchar *sql)
{
    char *err = NULL;
    if (sqlite3_exec (db, sql, NULL, NULL, &err) != SQLITE_OK)
    {
        g_warning ("SQLite exec failed (%s): %s", sql, err ? err : "(no msg)");
        sqlite3_free (err);
        return FALSE;
    }
    return TRUE;
}

static void
_g_paste_sqlite_backend_close_db (gpointer data)
{
    sqlite3 *db = data;
    if (db)
        sqlite3_close (db);
}

/* Open (or return cached) sqlite3 handle for @path. The pragma block + schema
 * creation is idempotent so we run it on every fresh open; cached handles
 * skip it. */
static sqlite3 *
_g_paste_sqlite_backend_open (const GPasteSqliteBackend *self,
                              const gchar               *path)
{
    GPasteSqliteBackendPrivate *priv = g_paste_sqlite_backend_get_instance_private ((GPasteSqliteBackend *) self);

    sqlite3 *db = g_hash_table_lookup (priv->dbs, path);
    if (db)
        return db;

    if (sqlite3_open_v2 (path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK)
    {
        g_warning ("Failed to open SQLite db %s: %s", path, db ? sqlite3_errmsg (db) : "(no handle)");
        sqlite3_close (db);
        return NULL;
    }

    if (!_g_paste_sqlite_backend_exec (db,
                                       "PRAGMA journal_mode=WAL;"
                                       "PRAGMA synchronous=NORMAL;"
                                       "PRAGMA foreign_keys=ON;"
                                       SCHEMA_SQL))
    {
        sqlite3_close (db);
        return NULL;
    }

    sqlite3_create_function (db, "REGEXP", 2,
                             SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                             NULL,
                             _g_paste_sqlite_backend_regex_udf,
                             NULL, NULL);

    g_hash_table_insert (priv->dbs, g_strdup (path), db);
    return db;
}

/* Drop a cached connection (e.g. after deleting the underlying file). */
static void
_g_paste_sqlite_backend_drop_cached (const GPasteSqliteBackend *self,
                                     const gchar               *path)
{
    GPasteSqliteBackendPrivate *priv = g_paste_sqlite_backend_get_instance_private ((GPasteSqliteBackend *) self);
    g_hash_table_remove (priv->dbs, path);
}

static void
_g_paste_sqlite_backend_insert_special_values (sqlite3          *db,
                                               const gchar      *uuid,
                                               const GPasteItem *item)
{
    const GSList *specials = g_paste_item_get_special_values (item);
    if (!specials)
        return;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2 (db,
                            "INSERT INTO special_values (item_uuid, mime, data) VALUES (?, ?, ?)",
                            -1, &stmt, NULL) != SQLITE_OK)
    {
        g_warning ("prepare INSERT special_values failed: %s", sqlite3_errmsg (db));
        return;
    }

    for (const GSList *l = specials; l; l = l->next)
    {
        const GPasteBinaryData *bd = l->data;
        const gchar *mime = g_paste_special_atom_get (g_paste_binary_data_get_mime (bd));
        GBytes *bytes = g_paste_binary_data_get_bytes (bd);
        if (!mime || !bytes)
            continue;

        gsize len = 0;
        gconstpointer data = g_bytes_get_data (bytes, &len);

        sqlite3_reset (stmt);
        sqlite3_clear_bindings (stmt);
        sqlite3_bind_text (stmt, 1, uuid, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (stmt, 2, mime, -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob (stmt, 3, data, (int) len, SQLITE_TRANSIENT);

        if (sqlite3_step (stmt) != SQLITE_DONE)
            g_warning ("INSERT special_values failed: %s", sqlite3_errmsg (db));
    }

    sqlite3_finalize (stmt);
}

static void
_g_paste_sqlite_backend_insert_image_metadata (sqlite3                *db,
                                               const gchar            *uuid,
                                               const GPasteImageItem  *image)
{
    const GDateTime *date = g_paste_image_item_get_date (image);
    if (!date)
        return;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2 (db,
                            "INSERT INTO image_metadata (item_uuid, date) VALUES (?, ?)",
                            -1, &stmt, NULL) != SQLITE_OK)
    {
        g_warning ("prepare INSERT image_metadata failed: %s", sqlite3_errmsg (db));
        return;
    }

    sqlite3_bind_text (stmt, 1, uuid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64 (stmt, 2, g_date_time_to_unix ((GDateTime *) date));

    if (sqlite3_step (stmt) != SQLITE_DONE)
        g_warning ("INSERT image_metadata failed: %s", sqlite3_errmsg (db));

    sqlite3_finalize (stmt);
}

/* Insert one item at the given @clip_order. Caller wraps in a transaction. */
static void
_g_paste_sqlite_backend_insert_item (sqlite3          *db,
                                     const GPasteItem *item,
                                     gdouble           clip_order)
{
    const gchar *kind = g_paste_item_get_kind (item);
    /* Password items live in memory only by design. */
    if (g_paste_str_equal (kind, "Password"))
        return;

    const gchar *uuid    = g_paste_item_get_uuid (item);
    const gchar *value   = g_paste_item_get_value (item);
    const gchar *display = g_paste_item_get_display_string (item);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2 (db,
                            "INSERT INTO items (uuid, kind, value, display_string, hash, size,"
                            "                   created_date, clip_order)"
                            " VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                            -1, &stmt, NULL) != SQLITE_OK)
    {
        g_warning ("prepare INSERT items failed: %s", sqlite3_errmsg (db));
        return;
    }

    sqlite3_bind_text  (stmt, 1, uuid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 2, kind, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 3, value ? value : "", -1, SQLITE_TRANSIENT);
    if (display)
        sqlite3_bind_text (stmt, 4, display, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null (stmt, 4);
    sqlite3_bind_int64 (stmt, 5, _g_paste_sqlite_backend_hash_u64 (value));
    sqlite3_bind_int64 (stmt, 6, (gint64) g_paste_item_get_size (item));
    sqlite3_bind_int64 (stmt, 7, g_get_real_time () / G_USEC_PER_SEC);
    sqlite3_bind_double (stmt, 8, clip_order);

    if (sqlite3_step (stmt) != SQLITE_DONE)
        g_warning ("INSERT items failed: %s", sqlite3_errmsg (db));

    sqlite3_finalize (stmt);

    if (_G_PASTE_IS_IMAGE_ITEM (item))
        _g_paste_sqlite_backend_insert_image_metadata (db, uuid, _G_PASTE_IMAGE_ITEM (item));

    _g_paste_sqlite_backend_insert_special_values (db, uuid, item);
}

static void
_g_paste_sqlite_backend_restore_special_values (sqlite3     *db,
                                                const gchar *uuid,
                                                GPasteItem  *item)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2 (db,
                            "SELECT mime, data FROM special_values WHERE item_uuid = ?",
                            -1, &stmt, NULL) != SQLITE_OK)
        return;

    sqlite3_bind_text (stmt, 1, uuid, -1, SQLITE_TRANSIENT);

    while (sqlite3_step (stmt) == SQLITE_ROW)
    {
        const gchar      *mime = (const gchar *) sqlite3_column_text (stmt, 0);
        GPasteSpecialAtom atom = _g_paste_sqlite_backend_atom_from_mime (mime);
        if (atom == G_PASTE_SPECIAL_ATOM_INVALID)
        {
            g_warning ("Unknown mime in special_values: %s", mime ? mime : "(null)");
            continue;
        }

        gconstpointer blob = sqlite3_column_blob (stmt, 1);
        int           len  = sqlite3_column_bytes (stmt, 1);
        g_autoptr (GBytes) bytes = g_bytes_new (blob, (gsize) len);
        GPasteBinaryData *bd     = g_paste_binary_data_new (atom, bytes);

        g_paste_item_add_special_value (item, bd);
    }

    sqlite3_finalize (stmt);
}

static GDateTime *
_g_paste_sqlite_backend_lookup_image_date (sqlite3     *db,
                                           const gchar *uuid)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2 (db,
                            "SELECT date FROM image_metadata WHERE item_uuid = ?",
                            -1, &stmt, NULL) != SQLITE_OK)
        return NULL;

    sqlite3_bind_text (stmt, 1, uuid, -1, SQLITE_TRANSIENT);

    GDateTime *date = NULL;
    if (sqlite3_step (stmt) == SQLITE_ROW)
        date = g_date_time_new_from_unix_local (sqlite3_column_int64 (stmt, 0));

    sqlite3_finalize (stmt);
    return date;
}

static GPasteItem *
_g_paste_sqlite_backend_build_item (sqlite3     *db,
                                    const gchar *uuid,
                                    const gchar *kind,
                                    const gchar *value,
                                    const gchar *display,
                                    gboolean     images_support)
{
    GPasteItem *item = NULL;

    if (g_paste_str_equal (kind, "Text"))
        item = g_paste_text_item_new (value);
    else if (g_paste_str_equal (kind, "Uris"))
        item = g_paste_uris_item_new_from_str (value);
    else if (g_paste_str_equal (kind, "Color"))
        item = g_paste_color_item_new_from_str (value);
    else if (g_paste_str_equal (kind, "Image"))
    {
        if (!images_support)
            return NULL;

        g_autoptr (GDateTime) date = _g_paste_sqlite_backend_lookup_image_date (db, uuid);
        if (!date)
            return NULL;

        /* Until C4 lands, items.value holds the on-disk path and we still need a
         * checksum to rebuild a GPasteImageItem; derive it from the basename of
         * the path, mirroring the file backend's storage layout. */
        g_autofree gchar *basename = g_path_get_basename (value);
        gchar *dot = strrchr (basename, '.');
        if (dot)
            *dot = '\0';

        item = g_paste_image_item_new_from_file (value, date, basename);
    }
    else
    {
        g_warning ("Unknown item kind in sqlite db: %s", kind);
        return NULL;
    }

    if (!item)
        return NULL;

    g_paste_item_set_uuid (item, uuid);
    if (display)
        g_paste_item_set_display_string (item, g_strdup (display));

    _g_paste_sqlite_backend_restore_special_values (db, uuid, item);

    return item;
}

static void
g_paste_sqlite_backend_read_history_file (const GPasteStorageBackend *self,
                                          const gchar                *history_file_path,
                                          GList                     **history,
                                          gsize                      *size)
{
    *history = NULL;
    *size = 0;

    /* Mirror file_backend: an absent file means "empty history", not "create the
     * db". We don't want a stray read on a never-written-to history to create a
     * file on disk. */
    if (!g_file_test (history_file_path, G_FILE_TEST_EXISTS))
        return;

    sqlite3 *db = _g_paste_sqlite_backend_open ((const GPasteSqliteBackend *) self, history_file_path);
    if (!db)
        return;

    const GPasteSettings *settings = _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->get_settings (self);
    gboolean              images_support = g_paste_settings_get_images_support (settings);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2 (db,
                            "SELECT uuid, kind, value, display_string, size"
                            "  FROM items"
                            " ORDER BY sticky_clip_order DESC, clip_order DESC",
                            -1, &stmt, NULL) != SQLITE_OK)
    {
        g_warning ("prepare SELECT items failed: %s", sqlite3_errmsg (db));
        return;
    }

    GList *out = NULL;
    gsize  total = 0;

    while (sqlite3_step (stmt) == SQLITE_ROW)
    {
        const gchar *uuid    = (const gchar *) sqlite3_column_text (stmt, 0);
        const gchar *kind    = (const gchar *) sqlite3_column_text (stmt, 1);
        const gchar *value   = (const gchar *) sqlite3_column_text (stmt, 2);
        const gchar *display = (const gchar *) sqlite3_column_text (stmt, 3);

        GPasteItem *item = _g_paste_sqlite_backend_build_item (db, uuid, kind, value, display, images_support);
        if (!item)
            continue;

        total += g_paste_item_get_size (item);
        out = g_list_prepend (out, item);
    }

    sqlite3_finalize (stmt);

    *history = g_list_reverse (out);
    *size    = total;
}

static void
g_paste_sqlite_backend_write_history_file (const GPasteStorageBackend *self,
                                           const gchar                *history_file_path,
                                           const GList                *history)
{
    const GPasteSettings *settings = _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->get_settings (self);

    if (!g_paste_util_ensure_history_dir_exists ())
        return;

    /* Mirror file_backend: if change tracking is disabled, delete the db file (and
     * its WAL/SHM sidecars) instead of writing it. */
    if (!g_paste_settings_get_track_changes (settings))
    {
        _g_paste_sqlite_backend_drop_cached ((const GPasteSqliteBackend *) self, history_file_path);

        g_autoptr (GFile) file = g_file_new_for_path (history_file_path);
        g_autoptr (GError) error = NULL;
        if (!g_file_delete (file, NULL, &error) && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            g_warning ("Failed to delete history db: %s", error->message);

        /* Best-effort WAL/SHM cleanup. */
        const gchar *suffixes[] = { "-wal", "-shm" };
        for (gsize i = 0; i < G_N_ELEMENTS (suffixes); ++i)
        {
            g_autofree gchar *side = g_strconcat (history_file_path, suffixes[i], NULL);
            g_autoptr (GFile) sf = g_file_new_for_path (side);
            g_autoptr (GError) e = NULL;
            if (!g_file_delete (sf, NULL, &e) && !g_error_matches (e, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
                g_warning ("Failed to delete %s: %s", side, e->message);
        }
        return;
    }

    sqlite3 *db = _g_paste_sqlite_backend_open ((const GPasteSqliteBackend *) self, history_file_path);
    if (!db)
        return;

    if (!_g_paste_sqlite_backend_exec (db, "BEGIN IMMEDIATE"))
        return;

    if (!_g_paste_sqlite_backend_exec (db, "DELETE FROM items"))
    {
        _g_paste_sqlite_backend_exec (db, "ROLLBACK");
        return;
    }

    /* Walk @history head-to-tail. Head should land with the highest clip_order so
     * the DESC scan in read_history returns the same order. If the caller hands
     * us a longer list than max-history-size allows, drop the tail to keep the
     * persisted table within the configured cap (same invariant the C2 add
     * path maintains). */
    guint64 limit  = g_paste_settings_get_max_history_size (settings);
    guint64 length = g_list_length ((GList *) history);
    if (limit > 0 && length > limit)
        length = limit;

    gdouble clip = (gdouble) length;
    guint64 i    = 0;

    for (const GList *l = history; l && i < length; l = l->next, ++i, clip -= 1.0)
        _g_paste_sqlite_backend_insert_item (db, l->data, clip);

    if (!_g_paste_sqlite_backend_exec (db, "COMMIT"))
        _g_paste_sqlite_backend_exec (db, "ROLLBACK");
}

static void
g_paste_sqlite_backend_delete_history (const GPasteStorageBackend *self,
                                       const gchar                *name,
                                       GError                   **error)
{
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "db");
    _g_paste_sqlite_backend_drop_cached ((const GPasteSqliteBackend *) self, path);

    g_autoptr (GFile) file = g_file_new_for_path (path);
    g_file_delete (file, NULL, error);

    /* Best-effort sidecar cleanup; ignore NOT_FOUND. */
    const gchar *suffixes[] = { "-wal", "-shm" };
    for (gsize i = 0; i < G_N_ELEMENTS (suffixes); ++i)
    {
        g_autofree gchar *side = g_strconcat (path, suffixes[i], NULL);
        g_autoptr (GFile) sf = g_file_new_for_path (side);
        g_autoptr (GError) e = NULL;
        if (!g_file_delete (sf, NULL, &e) && !g_error_matches (e, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            g_warning ("Failed to delete %s: %s", side, e->message);
    }
}

static GStrv
g_paste_sqlite_backend_list_histories (const GPasteStorageBackend *self G_GNUC_UNUSED,
                                       GError                   **error)
{
    g_autoptr (GStrvBuilder) names = g_strv_builder_new ();
    g_autoptr (GFile) dir = g_paste_util_get_history_dir ();
    g_autoptr (GFileEnumerator) enumerator = g_file_enumerate_children (dir,
                                                                        G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                                                                        G_FILE_QUERY_INFO_NONE,
                                                                        NULL,
                                                                        error);
    if (error && *error)
    {
        if ((*error)->domain == G_IO_ERROR && (*error)->code == G_IO_ERROR_NOT_FOUND)
        {
            g_clear_error (error);
            return g_strv_builder_end (names);
        }
        return NULL;
    }

    GFileInfo *info;
    while ((info = g_file_enumerator_next_file (enumerator, NULL, error)))
    {
        g_autoptr (GFileInfo) h = info;
        if (error && *error)
            return NULL;

        const gchar *raw = g_file_info_get_display_name (h);
        if (!g_str_has_suffix (raw, ".db"))
            continue;

        g_autofree gchar *name = g_strndup (raw, strlen (raw) - 3);
        g_strv_builder_take (names, g_steal_pointer (&name));
    }

    return g_strv_builder_end (names);
}

static const gchar *
g_paste_sqlite_backend_get_extension (const GPasteStorageBackend *self G_GNUC_UNUSED)
{
    return "db";
}

/* --- Incremental write path (C2). --- */

/* SELECT COALESCE(MAX(clip_order), 0) FROM items. */
static gdouble
_g_paste_sqlite_backend_max_clip_order (sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    gdouble max = 0.0;

    if (sqlite3_prepare_v2 (db, "SELECT COALESCE(MAX(clip_order), 0) FROM items", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_step (stmt) == SQLITE_ROW)
            max = sqlite3_column_double (stmt, 0);
        sqlite3_finalize (stmt);
    }

    return max;
}

/* Returns a freshly-allocated uuid for an existing row with matching (hash, kind),
 * or NULL if no duplicate is on file. */
static gchar *
_g_paste_sqlite_backend_lookup_dup_uuid (sqlite3     *db,
                                         gint64       hash,
                                         const gchar *kind)
{
    sqlite3_stmt *stmt = NULL;
    gchar *uuid = NULL;

    if (sqlite3_prepare_v2 (db,
                            "SELECT uuid FROM items WHERE hash = ? AND kind = ? LIMIT 1",
                            -1, &stmt, NULL) == SQLITE_OK)
    {
        sqlite3_bind_int64 (stmt, 1, hash);
        sqlite3_bind_text  (stmt, 2, kind, -1, SQLITE_TRANSIENT);

        if (sqlite3_step (stmt) == SQLITE_ROW)
            uuid = g_strdup ((const gchar *) sqlite3_column_text (stmt, 0));

        sqlite3_finalize (stmt);
    }

    return uuid;
}

/* DELETE the lowest-priority rows until the table is at most @limit items. The
 * ordering matches read_history_file's SELECT, with pinned items winning over
 * stickies winning over plain clip_order. pinned = 0 is the only level present
 * in C2 — the pinned column is plumbed through for future PRs. */
static void
_g_paste_sqlite_backend_truncate (sqlite3 *db,
                                  guint64  limit)
{
    if (!limit)
        return;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2 (db,
                            "DELETE FROM items"
                            " WHERE pinned = 0"
                            "   AND uuid NOT IN ("
                            "       SELECT uuid FROM items"
                            "        ORDER BY pinned DESC, sticky_clip_order DESC, clip_order DESC"
                            "        LIMIT ?)",
                            -1, &stmt, NULL) == SQLITE_OK)
    {
        sqlite3_bind_int64 (stmt, 1, (gint64) limit);
        if (sqlite3_step (stmt) != SQLITE_DONE)
            g_warning ("truncate items failed: %s", sqlite3_errmsg (db));
        sqlite3_finalize (stmt);
    }
}

static sqlite3 *
_g_paste_sqlite_backend_open_for_name (const GPasteStorageBackend *self,
                                       const gchar                *name)
{
    const GPasteSettings *settings = _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->get_settings (self);
    if (!g_paste_util_ensure_history_dir_exists ())
        return NULL;
    if (!g_paste_settings_get_track_changes (settings))
        return NULL;

    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "db");
    return _g_paste_sqlite_backend_open ((const GPasteSqliteBackend *) self, path);
}

static void
g_paste_sqlite_backend_add_item (const GPasteStorageBackend *self,
                                 const gchar                *name,
                                 const GPasteItem           *item,
                                 const GList                *history G_GNUC_UNUSED)
{
    const gchar *kind = g_paste_item_get_kind (item);
    if (g_paste_str_equal (kind, "Password"))
        return;

    sqlite3 *db = _g_paste_sqlite_backend_open_for_name (self, name);
    if (!db)
        return;

    if (!_g_paste_sqlite_backend_exec (db, "BEGIN IMMEDIATE"))
        return;

    const gchar *value      = g_paste_item_get_value (item);
    gint64       hash       = _g_paste_sqlite_backend_hash_u64 (value);
    gdouble      next_order = _g_paste_sqlite_backend_max_clip_order (db) + 1.0;
    g_autofree gchar *dup   = _g_paste_sqlite_backend_lookup_dup_uuid (db, hash, kind);

    if (dup)
    {
        /* Existing row stays in place under its uuid; only its clip_order is
         * bumped so the latest paste appears at the head on the next read. */
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2 (db,
                                "UPDATE items SET clip_order = ? WHERE uuid = ?",
                                -1, &stmt, NULL) == SQLITE_OK)
        {
            sqlite3_bind_double (stmt, 1, next_order);
            sqlite3_bind_text   (stmt, 2, dup, -1, SQLITE_TRANSIENT);
            sqlite3_step (stmt);
            sqlite3_finalize (stmt);
        }
    }
    else
    {
        _g_paste_sqlite_backend_insert_item (db, item, next_order);
    }

    const GPasteSettings *settings = _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->get_settings (self);
    _g_paste_sqlite_backend_truncate (db, g_paste_settings_get_max_history_size (settings));

    if (!_g_paste_sqlite_backend_exec (db, "COMMIT"))
        _g_paste_sqlite_backend_exec (db, "ROLLBACK");
}

static void
g_paste_sqlite_backend_remove_item (const GPasteStorageBackend *self,
                                    const gchar                *name,
                                    const gchar                *uuid)
{
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "db");
    if (!g_file_test (path, G_FILE_TEST_EXISTS))
        return;

    sqlite3 *db = _g_paste_sqlite_backend_open ((const GPasteSqliteBackend *) self, path);
    if (!db)
        return;

    if (!_g_paste_sqlite_backend_exec (db, "BEGIN IMMEDIATE"))
        return;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2 (db, "DELETE FROM items WHERE uuid = ?", -1, &stmt, NULL) == SQLITE_OK)
    {
        sqlite3_bind_text (stmt, 1, uuid, -1, SQLITE_TRANSIENT);
        if (sqlite3_step (stmt) != SQLITE_DONE)
            g_warning ("DELETE FROM items WHERE uuid failed: %s", sqlite3_errmsg (db));
        sqlite3_finalize (stmt);
    }

    if (!_g_paste_sqlite_backend_exec (db, "COMMIT"))
        _g_paste_sqlite_backend_exec (db, "ROLLBACK");
}

static void
g_paste_sqlite_backend_replace_item (const GPasteStorageBackend *self,
                                     const gchar                *name,
                                     const gchar                *old_uuid,
                                     const GPasteItem           *item)
{
    if (g_paste_str_equal (g_paste_item_get_kind (item), "Password"))
        return;

    sqlite3 *db = _g_paste_sqlite_backend_open_for_name (self, name);
    if (!db)
        return;

    if (!_g_paste_sqlite_backend_exec (db, "BEGIN IMMEDIATE"))
        return;

    /* Inherit the old row's clip_order so the replacement keeps the same
     * position in the read order. If the old row is gone (race / programmer
     * error), fall back to MAX+1 so we don't lose the new item. */
    sqlite3_stmt *stmt    = NULL;
    gdouble       clip    = _g_paste_sqlite_backend_max_clip_order (db) + 1.0;
    gboolean      had_old = FALSE;

    if (sqlite3_prepare_v2 (db, "SELECT clip_order FROM items WHERE uuid = ?",
                            -1, &stmt, NULL) == SQLITE_OK)
    {
        sqlite3_bind_text (stmt, 1, old_uuid, -1, SQLITE_TRANSIENT);
        if (sqlite3_step (stmt) == SQLITE_ROW)
        {
            clip = sqlite3_column_double (stmt, 0);
            had_old = TRUE;
        }
        sqlite3_finalize (stmt);
    }

    if (had_old &&
        sqlite3_prepare_v2 (db, "DELETE FROM items WHERE uuid = ?",
                            -1, &stmt, NULL) == SQLITE_OK)
    {
        sqlite3_bind_text (stmt, 1, old_uuid, -1, SQLITE_TRANSIENT);
        if (sqlite3_step (stmt) != SQLITE_DONE)
            g_warning ("DELETE old uuid in replace failed: %s", sqlite3_errmsg (db));
        sqlite3_finalize (stmt);
    }

    _g_paste_sqlite_backend_insert_item (db, item, clip);

    if (!_g_paste_sqlite_backend_exec (db, "COMMIT"))
        _g_paste_sqlite_backend_exec (db, "ROLLBACK");
}

static void
g_paste_sqlite_backend_clear_history (const GPasteStorageBackend *self,
                                      const gchar                *name)
{
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "db");
    if (!g_file_test (path, G_FILE_TEST_EXISTS))
        return;

    sqlite3 *db = _g_paste_sqlite_backend_open ((const GPasteSqliteBackend *) self, path);
    if (!db)
        return;

    if (!_g_paste_sqlite_backend_exec (db, "BEGIN IMMEDIATE"))
        return;

    if (!_g_paste_sqlite_backend_exec (db, "DELETE FROM items"))
    {
        _g_paste_sqlite_backend_exec (db, "ROLLBACK");
        return;
    }

    if (!_g_paste_sqlite_backend_exec (db, "COMMIT"))
        _g_paste_sqlite_backend_exec (db, "ROLLBACK");
}

static void
g_paste_sqlite_backend_dispose (GObject *object)
{
    GPasteSqliteBackendPrivate *priv = g_paste_sqlite_backend_get_instance_private (G_PASTE_SQLITE_BACKEND (object));

    g_clear_pointer (&priv->dbs, g_hash_table_destroy);

    G_OBJECT_CLASS (g_paste_sqlite_backend_parent_class)->dispose (object);
}

static void
g_paste_sqlite_backend_class_init (GPasteSqliteBackendClass *klass)
{
    GPasteStorageBackendClass *storage_class = G_PASTE_STORAGE_BACKEND_CLASS (klass);

    storage_class->read_history_file  = g_paste_sqlite_backend_read_history_file;
    storage_class->write_history_file = g_paste_sqlite_backend_write_history_file;
    storage_class->get_extension      = g_paste_sqlite_backend_get_extension;
    storage_class->delete_history     = g_paste_sqlite_backend_delete_history;
    storage_class->list_histories     = g_paste_sqlite_backend_list_histories;

    storage_class->add_item       = g_paste_sqlite_backend_add_item;
    storage_class->remove_item    = g_paste_sqlite_backend_remove_item;
    storage_class->replace_item   = g_paste_sqlite_backend_replace_item;
    storage_class->clear_history  = g_paste_sqlite_backend_clear_history;

    G_OBJECT_CLASS (klass)->dispose = g_paste_sqlite_backend_dispose;
}

static void
g_paste_sqlite_backend_init (GPasteSqliteBackend *self)
{
    GPasteSqliteBackendPrivate *priv = g_paste_sqlite_backend_get_instance_private (self);

    priv->dbs = g_hash_table_new_full (g_str_hash, g_str_equal,
                                       g_free,
                                       _g_paste_sqlite_backend_close_db);
}
