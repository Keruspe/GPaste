// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-util.h>

#include <gpaste-daemon/gpaste-color-item.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-sqlite-backend.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-uris-item.h>

#include <sqlite3.h>

/* The SQLite storage backend: one database per history (<name>.db in the
 * history dir), storing the same data as the plain XML backend. It implements
 * the incremental vfuncs, so the saver feeds it per-operation changes instead
 * of full snapshots.
 *
 * Items live in an `items` table ordered by a monotonic `rank` (highest =
 * front of the history, so adds and selects never renumber anything), with
 * their extra MIME payloads in a `special_values` child table. The schema is
 * versioned through PRAGMA user_version so it can evolve: older databases are
 * migrated stepwise on open, newer ones are refused (every operation then
 * no-ops) rather than corrupted.
 *
 * Like the plain XML backend, password items are never persisted (the file is
 * user-readable); the `name` column is reserved for a future encrypted
 * variant. Images stay external PNG files, the item value being their path. */

#define G_PASTE_SQLITE_SCHEMA_VERSION 1

/* Far beyond any reachable rank (one increment per add/select), but cheap to
 * guard against: past this, ranks are compacted back to 1..N on open. */
#define G_PASTE_SQLITE_RANK_COMPACT_THRESHOLD (G_GINT64_CONSTANT (1) << 62)

struct _GPasteSqliteBackend
{
    GPasteStorageBackend parent_instance;
};

typedef struct
{
    /* The one open connection, lazily (re)opened for the last used database.
     * The lock serializes the vfuncs: the saver runs writes one at a time, but
     * a background load can overlap an in-flight write. */
    sqlite3 *db;
    gchar   *db_path;
    GMutex   lock;
} GPasteSqliteBackendPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (SqliteBackend, sqlite_backend, G_PASTE_TYPE_STORAGE_BACKEND)

static GPasteSqliteBackendPrivate *
g_paste_sqlite_backend_get_priv (const GPasteStorageBackend *self)
{
    return g_paste_sqlite_backend_get_instance_private (G_PASTE_SQLITE_BACKEND ((gpointer) self));
}

static void
g_paste_sqlite_backend_close (sqlite3 *db)
{
    sqlite3_close (db);
}

static gboolean
g_paste_sqlite_backend_exec (sqlite3     *db,
                             const gchar *sql)
{
    gchar *err = NULL;

    if (sqlite3_exec (db, sql, NULL, NULL, &err) != SQLITE_OK)
    {
        g_warning ("sqlite: failed to run “%s”: %s", sql, err);
        sqlite3_free (err);
        return FALSE;
    }

    return TRUE;
}

/* Finish an open transaction: COMMIT when @success, ROLLBACK otherwise (also
 * rolling back when the COMMIT itself fails). Returns the effective success. */
static gboolean
g_paste_sqlite_backend_finish_transaction (sqlite3  *db,
                                           gboolean  success)
{
    if (!g_paste_sqlite_backend_exec (db, (success) ? "COMMIT;" : "ROLLBACK;") && success)
    {
        g_paste_sqlite_backend_exec (db, "ROLLBACK;");
        return FALSE;
    }

    return success;
}

/* Run a query expected to produce a single integer (e.g. an aggregate or a
 * PRAGMA), returning @fallback when it produces nothing or fails. */
static gint64
g_paste_sqlite_backend_query_int64 (sqlite3     *db,
                                    const gchar *sql,
                                    gint64       fallback)
{
    sqlite3_stmt *stmt = NULL;
    gint64 value = fallback;

    if (sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        g_warning ("sqlite: failed to prepare “%s”: %s", sql, sqlite3_errmsg (db));
        return fallback;
    }

    if (sqlite3_step (stmt) == SQLITE_ROW)
        value = sqlite3_column_int64 (stmt, 0);

    sqlite3_finalize (stmt);

    return value;
}

static gboolean
g_paste_sqlite_backend_create_schema (sqlite3 *db)
{
    return g_paste_sqlite_backend_exec (db,
        "CREATE TABLE IF NOT EXISTS items ("
        "    id       INTEGER PRIMARY KEY,"
        "    uuid     TEXT    NOT NULL UNIQUE,"
        "    kind     TEXT    NOT NULL,"
        "    value    TEXT    NOT NULL,"
        "    rank     INTEGER NOT NULL," /* highest = front of the history */
        "    date     INTEGER,"          /* Image: unix seconds */
        "    checksum TEXT,"             /* Image: hex sha256 */
        "    name     TEXT"              /* Password: reserved for an encrypted variant */
        ");"
        "CREATE UNIQUE INDEX IF NOT EXISTS items_rank ON items (rank DESC);"
        "CREATE TABLE IF NOT EXISTS special_values ("
        "    item_id  INTEGER NOT NULL REFERENCES items (id) ON DELETE CASCADE,"
        "    position INTEGER NOT NULL,"
        "    mime     TEXT    NOT NULL," /* GPasteSpecialAtom value nick */
        "    data     BLOB    NOT NULL,"
        "    PRIMARY KEY (item_id, position)"
        ");");
}

static gboolean
g_paste_sqlite_backend_migrate_schema (sqlite3 *db,
                                       gint64   from)
{
    /* One case per historical schema version, each falling through to the next
     * so any old database upgrades stepwise to the current version. */
    switch (from)
    {
    default:
        (void) db;
        return TRUE;
    }
}

/* Get the (cached) connection for @db_path, opening and preparing the database
 * as needed. Returns NULL (and warns) when the database cannot be used, e.g.
 * when it was created by a newer GPaste: every operation then no-ops instead
 * of risking the data. Must be called with the backend lock held. */
static sqlite3 *
g_paste_sqlite_backend_open (const GPasteStorageBackend *self,
                             const gchar                *db_path)
{
    GPasteSqliteBackendPrivate *priv = g_paste_sqlite_backend_get_priv (self);

    if (priv->db && g_paste_str_equal (priv->db_path, db_path))
        return priv->db;

    g_clear_pointer (&priv->db, g_paste_sqlite_backend_close);
    g_clear_pointer (&priv->db_path, g_free);

    if (!g_paste_util_ensure_history_dir_exists ())
        return NULL;

    sqlite3 *db = NULL;

    if (sqlite3_open_v2 (db_path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL) != SQLITE_OK)
    {
        g_warning ("sqlite: failed to open “%s”: %s", db_path, db ? sqlite3_errmsg (db) : "out of memory");
        sqlite3_close (db);
        return NULL;
    }

    sqlite3_busy_timeout (db, 5000);

    if (!g_paste_sqlite_backend_exec (db,
                                      "PRAGMA journal_mode = WAL;"
                                      "PRAGMA synchronous = NORMAL;"
                                      "PRAGMA foreign_keys = ON;"))
    {
        sqlite3_close (db);
        return NULL;
    }

    gint64 version = g_paste_sqlite_backend_query_int64 (db, "PRAGMA user_version;", 0);
    gboolean ok;

    if (version > G_PASTE_SQLITE_SCHEMA_VERSION)
    {
        g_warning ("sqlite: “%s” uses schema version %" G_GINT64_FORMAT " from a newer GPaste (this one supports %d); not touching it",
                   db_path, version, G_PASTE_SQLITE_SCHEMA_VERSION);
        sqlite3_close (db);
        return NULL;
    }

    if (version == 0)
        ok = g_paste_sqlite_backend_create_schema (db);
    else
        ok = g_paste_sqlite_backend_migrate_schema (db, version);

    if (ok && version != G_PASTE_SQLITE_SCHEMA_VERSION)
        ok = g_paste_sqlite_backend_exec (db, "PRAGMA user_version = " G_STRINGIFY (G_PASTE_SQLITE_SCHEMA_VERSION) ";");

    if (!ok)
    {
        sqlite3_close (db);
        return NULL;
    }

    if (g_paste_sqlite_backend_query_int64 (db, "SELECT COALESCE (MAX (rank), 0) FROM items;", 0) > G_PASTE_SQLITE_RANK_COMPACT_THRESHOLD)
    {
        g_paste_sqlite_backend_exec (db,
                                     "UPDATE items SET rank = ranked.new_rank "
                                     "FROM (SELECT id, ROW_NUMBER () OVER (ORDER BY rank) AS new_rank FROM items) AS ranked "
                                     "WHERE items.id = ranked.id;");
    }

    priv->db = db;
    priv->db_path = g_strdup (db_path);

    return db;
}

/*****************/
/* Writing items */
/*****************/

static gboolean
g_paste_sqlite_backend_write_special_values (sqlite3          *db,
                                             gint64            item_id,
                                             const GPasteItem *item)
{
    const GSList *special_values = g_paste_item_get_special_values (item);

    if (!special_values)
        return TRUE;

    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2 (db, "INSERT INTO special_values (item_id, position, mime, data) VALUES (?, ?, ?, ?);", -1, &stmt, NULL) != SQLITE_OK)
    {
        g_warning ("sqlite: failed to prepare special value insertion: %s", sqlite3_errmsg (db));
        return FALSE;
    }

    GEnumClass *atom_class = g_type_class_ref (G_PASTE_TYPE_SPECIAL_ATOM);
    gboolean success = TRUE;
    gint64 position = 0;

    for (const GSList *val = special_values; success && val; val = val->next, ++position)
    {
        const GPasteBinaryData *value = val->data;
        const gchar *mime = g_enum_get_value (atom_class, g_paste_binary_data_get_mime (value))->value_nick;
        gsize data_length;
        gconstpointer data = g_bytes_get_data (g_paste_binary_data_get_bytes (value), &data_length);

        sqlite3_bind_int64 (stmt, 1, item_id);
        sqlite3_bind_int64 (stmt, 2, position);
        sqlite3_bind_text (stmt, 3, mime, -1, SQLITE_STATIC);
        sqlite3_bind_blob64 (stmt, 4, data, data_length, SQLITE_STATIC);

        if (sqlite3_step (stmt) != SQLITE_DONE)
        {
            g_warning ("sqlite: failed to write a special value: %s", sqlite3_errmsg (db));
            success = FALSE;
        }

        sqlite3_reset (stmt);
        sqlite3_clear_bindings (stmt);
    }

    g_type_class_unref (atom_class);
    sqlite3_finalize (stmt);

    return success;
}

/* Replace an item's stored special values with the ones it carries. */
static gboolean
g_paste_sqlite_backend_rewrite_special_values (sqlite3          *db,
                                               gint64            item_id,
                                               const GPasteItem *item)
{
    sqlite3_stmt *del = NULL;

    if (sqlite3_prepare_v2 (db, "DELETE FROM special_values WHERE item_id = ?;", -1, &del, NULL) != SQLITE_OK)
    {
        g_warning ("sqlite: failed to prepare special value cleanup: %s", sqlite3_errmsg (db));
        return FALSE;
    }

    sqlite3_bind_int64 (del, 1, item_id);

    gboolean success = (sqlite3_step (del) == SQLITE_DONE);

    sqlite3_finalize (del);

    return success && g_paste_sqlite_backend_write_special_values (db, item_id, item);
}

/* Insert @item with @rank, or move the already-stored item with the same uuid
 * to @rank (its value never changes, only its position). Special values are
 * rewritten from the item either way. */
static gboolean
g_paste_sqlite_backend_upsert_item (sqlite3          *db,
                                    const GPasteItem *item,
                                    gint64            rank)
{
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2 (db,
                            "INSERT INTO items (uuid, kind, value, rank, date, checksum, name) VALUES (?, ?, ?, ?, ?, ?, ?) "
                            "ON CONFLICT (uuid) DO UPDATE SET rank = excluded.rank "
                            "RETURNING id;",
                            -1, &stmt, NULL) != SQLITE_OK)
    {
        g_warning ("sqlite: failed to prepare item insertion: %s", sqlite3_errmsg (db));
        return FALSE;
    }

    sqlite3_bind_text (stmt, 1, g_paste_item_get_uuid (item), -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 2, g_paste_item_get_kind (item), -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 3, g_paste_item_get_real_value (item), -1, SQLITE_STATIC);
    sqlite3_bind_int64 (stmt, 4, rank);

    if (_G_PASTE_IS_IMAGE_ITEM (item))
    {
        const GPasteImageItem *image = _G_PASTE_IMAGE_ITEM (item);
        const gchar *checksum = g_paste_image_item_get_checksum (image);

        sqlite3_bind_int64 (stmt, 5, g_date_time_to_unix ((GDateTime *) g_paste_image_item_get_date (image)));
        if (checksum)
            sqlite3_bind_text (stmt, 6, checksum, -1, SQLITE_STATIC);
    }

    gboolean success = (sqlite3_step (stmt) == SQLITE_ROW);
    gint64 item_id = success ? sqlite3_column_int64 (stmt, 0) : 0;

    if (!success)
        g_warning ("sqlite: failed to write an item: %s", sqlite3_errmsg (db));

    sqlite3_finalize (stmt);

    return success && g_paste_sqlite_backend_rewrite_special_values (db, item_id, item);
}

static void
g_paste_sqlite_backend_write_history_file (const GPasteStorageBackend *self,
                                           const gchar                *history_file_path,
                                           const GList                *history)
{
    GPasteSqliteBackendPrivate *priv = g_paste_sqlite_backend_get_priv (self);
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&priv->lock);
    sqlite3 *db = g_paste_sqlite_backend_open (self, history_file_path);

    if (!db)
        return;

    /* Count what we'll actually store so the front item gets the highest rank. */
    gint64 rank = 0;

    for (const GList *h = history; h; h = g_list_next (h))
    {
        if (!g_paste_str_equal (g_paste_item_get_kind (h->data), "Password"))
            ++rank;
    }

    /* One transaction, so replacing the whole content is atomic: a failure
     * (or crash) rolls back to the previous state instead of losing data. */
    if (!g_paste_sqlite_backend_exec (db, "BEGIN IMMEDIATE;"))
        return;

    gboolean success = g_paste_sqlite_backend_exec (db, "DELETE FROM items;");

    for (const GList *h = history; success && h; h = g_list_next (h))
    {
        const GPasteItem *item = h->data;

        if (g_paste_str_equal (g_paste_item_get_kind (item), "Password"))
            continue;

        success = g_paste_sqlite_backend_upsert_item (db, item, rank--);
    }

    g_paste_sqlite_backend_finish_transaction (db, success);
}

/*****************/
/* Reading items */
/*****************/

static void
g_paste_sqlite_backend_read_special_values (sqlite3_stmt *stmt,
                                            GEnumClass   *atom_class,
                                            gint64        item_id,
                                            GPasteItem   *item)
{
    sqlite3_bind_int64 (stmt, 1, item_id);

    while (sqlite3_step (stmt) == SQLITE_ROW)
    {
        const gchar *mime = (const gchar *) sqlite3_column_text (stmt, 0);
        GEnumValue *gev = g_enum_get_value_by_nick (atom_class, mime);

        if (!gev)
        {
            g_warning ("sqlite: unknown mime: %s", mime);
            continue;
        }

        GBytes *bytes = g_bytes_new (sqlite3_column_blob (stmt, 1), sqlite3_column_bytes (stmt, 1));

        g_paste_item_add_special_value (item, g_paste_binary_data_new (gev->value, bytes));
    }

    sqlite3_reset (stmt);
    sqlite3_clear_bindings (stmt);
}

static GPasteItem *
g_paste_sqlite_backend_read_item (sqlite3_stmt *stmt,
                                  gboolean      images_support)
{
    const gchar *kind = (const gchar *) sqlite3_column_text (stmt, 2);
    const gchar *value = (const gchar *) sqlite3_column_text (stmt, 3);

    if (g_paste_str_equal (kind, "Text"))
        return g_paste_text_item_new (value);
    if (g_paste_str_equal (kind, "Uris"))
        return g_paste_uris_item_new_from_str (value);
    if (g_paste_str_equal (kind, "Password"))
        return g_paste_password_item_new ((const gchar *) sqlite3_column_text (stmt, 6), value);
    if (g_paste_str_equal (kind, "Color"))
        return g_paste_color_item_new_from_str (value);

    if (g_paste_str_equal (kind, "Image"))
    {
        if (images_support && sqlite3_column_type (stmt, 4) != SQLITE_NULL)
        {
            g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (sqlite3_column_int64 (stmt, 4));

            return g_paste_image_item_new_from_file (value, date, (const gchar *) sqlite3_column_text (stmt, 5));
        }

        g_autoptr (GFile) img_file = g_file_new_for_path (value);

        if (g_file_query_exists (img_file,
                                 NULL)) /* cancellable */
        {
            g_autoptr (GError) error = NULL;
            if (!g_file_delete (img_file, NULL, &error))
                g_warning ("Failed to delete leftover image: %s", error->message);
        }

        return NULL;
    }

    g_warning ("Unknown item kind: %s", kind);

    return NULL;
}

static void
g_paste_sqlite_backend_read_history_file (const GPasteStorageBackend *self,
                                          const gchar                *history_file_path,
                                          GList                     **history,
                                          gsize                      *size)
{
    const GPasteSettings *settings = _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->get_settings (self);
    GPasteSqliteBackendPrivate *priv = g_paste_sqlite_backend_get_priv (self);
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&priv->lock);
    /* Opening creates the database on first read, so a fresh history shows up
     * in listings just like the file backend's empty placeholder. */
    sqlite3 *db = g_paste_sqlite_backend_open (self, history_file_path);

    *history = NULL;
    *size = 0;

    if (!db)
        return;

    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2 (db, "SELECT id, uuid, kind, value, date, checksum, name FROM items ORDER BY rank DESC LIMIT ?;", -1, &stmt, NULL) != SQLITE_OK)
    {
        g_warning ("sqlite: failed to prepare history query: %s", sqlite3_errmsg (db));
        return;
    }

    sqlite3_bind_int64 (stmt, 1, g_paste_settings_get_max_history_size (settings));

    /* Prepared once for the whole load: reset and re-bound per item.
     * add_special_value prepends, so walking positions backwards rebuilds each
     * item's special values in their original order. */
    sqlite3_stmt *sv_stmt = NULL;

    if (sqlite3_prepare_v2 (db, "SELECT mime, data FROM special_values WHERE item_id = ? ORDER BY position DESC;", -1, &sv_stmt, NULL) != SQLITE_OK)
    {
        g_warning ("sqlite: failed to prepare special value query: %s", sqlite3_errmsg (db));
        sqlite3_finalize (stmt);
        return;
    }

    GEnumClass *atom_class = g_type_class_ref (G_PASTE_TYPE_SPECIAL_ATOM);
    gboolean images_support = g_paste_settings_get_images_support (settings);

    while (sqlite3_step (stmt) == SQLITE_ROW)
    {
        GPasteItem *item = g_paste_sqlite_backend_read_item (stmt, images_support);

        if (!item)
            continue;

        const gchar *uuid = (const gchar *) sqlite3_column_text (stmt, 1);

        if (uuid && g_uuid_string_is_valid (uuid))
            g_paste_item_set_uuid (item, uuid);

        g_paste_sqlite_backend_read_special_values (sv_stmt, atom_class, sqlite3_column_int64 (stmt, 0), item);

        *history = g_list_prepend (*history, item);
        *size += g_paste_item_get_size (item);
    }

    g_type_class_unref (atom_class);
    sqlite3_finalize (sv_stmt);
    sqlite3_finalize (stmt);

    *history = g_list_reverse (*history);
}

/***********************/
/* Incremental updates */
/***********************/

/* The history only ever hands us items and uuids it also reflects in the
 * snapshot, so the snapshot is the authoritative fallback: after applying the
 * granular hint, any stored row missing from it (items evicted by the size or
 * memory limits, or a deduplicated older copy — those never get their own
 * remove operation) is dropped. */
static void
g_paste_sqlite_backend_reconcile (sqlite3     *db,
                                  const GList *history)
{
    gint64 expected = 0;

    for (const GList *h = history; h; h = g_list_next (h))
    {
        if (!g_paste_str_equal (g_paste_item_get_kind (h->data), "Password"))
            ++expected;
    }

    /* The common case: nothing rode along, the store already matches. Only
     * build the uuid set (and scan the table) when it actually does not. */
    if (g_paste_sqlite_backend_query_int64 (db, "SELECT COUNT (*) FROM items;", expected) == expected)
        return;

    g_autoptr (GHashTable) uuids = g_hash_table_new (g_str_hash, g_str_equal);

    for (const GList *h = history; h; h = g_list_next (h))
    {
        const GPasteItem *item = h->data;

        if (!g_paste_str_equal (g_paste_item_get_kind (item), "Password"))
            g_hash_table_add (uuids, (gpointer) g_paste_item_get_uuid (item));
    }

    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2 (db, "SELECT uuid FROM items;", -1, &stmt, NULL) != SQLITE_OK)
    {
        g_warning ("sqlite: failed to prepare reconciliation query: %s", sqlite3_errmsg (db));
        return;
    }

    g_autoptr (GStrvBuilder) extra = g_strv_builder_new ();

    while (sqlite3_step (stmt) == SQLITE_ROW)
    {
        const gchar *uuid = (const gchar *) sqlite3_column_text (stmt, 0);

        if (!g_hash_table_contains (uuids, uuid))
            g_strv_builder_add (extra, uuid);
    }

    sqlite3_finalize (stmt);

    g_auto (GStrv) to_delete = g_strv_builder_end (extra);

    if (!to_delete || !*to_delete)
        return;

    sqlite3_stmt *del = NULL;

    if (sqlite3_prepare_v2 (db, "DELETE FROM items WHERE uuid = ?;", -1, &del, NULL) != SQLITE_OK)
    {
        g_warning ("sqlite: failed to prepare reconciliation cleanup: %s", sqlite3_errmsg (db));
        return;
    }

    for (GStrv uuid = to_delete; *uuid; ++uuid)
    {
        sqlite3_bind_text (del, 1, *uuid, -1, SQLITE_STATIC);

        if (sqlite3_step (del) != SQLITE_DONE)
            g_warning ("sqlite: failed to reconcile an item: %s", sqlite3_errmsg (db));

        sqlite3_reset (del);
        sqlite3_clear_bindings (del);
    }

    sqlite3_finalize (del);
}

/* An "add" is not always a pure insert: the same operation also covers
 * selecting an existing item (moved to the front) and re-adding a duplicate
 * (the older copy is dropped), and eviction of trailing items can ride along.
 * Hence upsert-and-reconcile rather than a bare INSERT. */
static void
g_paste_sqlite_backend_add_item (const GPasteStorageBackend *self,
                                 const gchar                *name,
                                 const GPasteItem           *item,
                                 const GList                *history)
{
    g_autofree gchar *db_path = g_paste_util_get_history_file_path (name, _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->get_extension (self));
    GPasteSqliteBackendPrivate *priv = g_paste_sqlite_backend_get_priv (self);
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&priv->lock);
    sqlite3 *db = g_paste_sqlite_backend_open (self, db_path);

    if (!db)
        return;

    if (!g_paste_sqlite_backend_exec (db, "BEGIN IMMEDIATE;"))
        return;

    gboolean success = TRUE;

    if (!g_paste_str_equal (g_paste_item_get_kind (item), "Password"))
    {
        gint64 rank = g_paste_sqlite_backend_query_int64 (db, "SELECT COALESCE (MAX (rank), 0) FROM items;", 0) + 1;

        success = g_paste_sqlite_backend_upsert_item (db, item, rank);
    }

    if (success)
        g_paste_sqlite_backend_reconcile (db, history);

    g_paste_sqlite_backend_finish_transaction (db, success);
}

static void
g_paste_sqlite_backend_remove_item (const GPasteStorageBackend *self,
                                    const gchar                *name,
                                    const gchar                *uuid)
{
    g_autofree gchar *db_path = g_paste_util_get_history_file_path (name, _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->get_extension (self));
    GPasteSqliteBackendPrivate *priv = g_paste_sqlite_backend_get_priv (self);
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&priv->lock);
    sqlite3 *db = g_paste_sqlite_backend_open (self, db_path);

    if (!db)
        return;

    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2 (db, "DELETE FROM items WHERE uuid = ?;", -1, &stmt, NULL) != SQLITE_OK)
    {
        g_warning ("sqlite: failed to prepare item removal: %s", sqlite3_errmsg (db));
        return;
    }

    sqlite3_bind_text (stmt, 1, uuid, -1, SQLITE_STATIC);

    if (sqlite3_step (stmt) != SQLITE_DONE)
        g_warning ("sqlite: failed to remove an item: %s", sqlite3_errmsg (db));

    sqlite3_finalize (stmt);
}

static void
g_paste_sqlite_backend_replace_item (const GPasteStorageBackend *self,
                                     const gchar                *name,
                                     const gchar                *old_uuid,
                                     const GPasteItem           *item)
{
    g_autofree gchar *db_path = g_paste_util_get_history_file_path (name, _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->get_extension (self));
    GPasteSqliteBackendPrivate *priv = g_paste_sqlite_backend_get_priv (self);
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&priv->lock);
    sqlite3 *db = g_paste_sqlite_backend_open (self, db_path);

    if (!db)
        return;

    /* An item turning into a password (set_password) must vanish from storage,
     * exactly as the plain XML backend drops passwords on rewrite. */
    if (g_paste_str_equal (g_paste_item_get_kind (item), "Password"))
    {
        g_clear_pointer (&locker, g_mutex_locker_free);
        g_paste_sqlite_backend_remove_item (self, name, old_uuid);
        return;
    }

    if (!g_paste_sqlite_backend_exec (db, "BEGIN IMMEDIATE;"))
        return;

    sqlite3_stmt *stmt = NULL;
    gboolean success = (sqlite3_prepare_v2 (db,
                                            "UPDATE items SET uuid = ?, kind = ?, value = ?, date = ?, checksum = ? WHERE uuid = ? "
                                            "RETURNING id;",
                                            -1, &stmt, NULL) == SQLITE_OK);

    if (!success)
    {
        g_warning ("sqlite: failed to prepare item replacement: %s", sqlite3_errmsg (db));
        g_paste_sqlite_backend_exec (db, "ROLLBACK;");
        return;
    }

    sqlite3_bind_text (stmt, 1, g_paste_item_get_uuid (item), -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 2, g_paste_item_get_kind (item), -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 3, g_paste_item_get_real_value (item), -1, SQLITE_STATIC);

    if (_G_PASTE_IS_IMAGE_ITEM (item))
    {
        const GPasteImageItem *image = _G_PASTE_IMAGE_ITEM (item);
        const gchar *checksum = g_paste_image_item_get_checksum (image);

        sqlite3_bind_int64 (stmt, 4, g_date_time_to_unix ((GDateTime *) g_paste_image_item_get_date (image)));
        if (checksum)
            sqlite3_bind_text (stmt, 5, checksum, -1, SQLITE_STATIC);
    }

    sqlite3_bind_text (stmt, 6, old_uuid, -1, SQLITE_STATIC);

    /* No row means the replaced item was never persisted (e.g. renaming a
     * password): nothing to update. */
    gint ret = sqlite3_step (stmt);
    gboolean found = (ret == SQLITE_ROW);
    gint64 item_id = found ? sqlite3_column_int64 (stmt, 0) : 0;

    if (ret != SQLITE_ROW && ret != SQLITE_DONE)
    {
        g_warning ("sqlite: failed to replace an item: %s", sqlite3_errmsg (db));
        success = FALSE;
    }

    sqlite3_finalize (stmt);

    if (success && found)
        success = g_paste_sqlite_backend_rewrite_special_values (db, item_id, item);

    g_paste_sqlite_backend_finish_transaction (db, success);
}

static void
g_paste_sqlite_backend_clear_history (const GPasteStorageBackend *self,
                                      const gchar                *name)
{
    g_autofree gchar *db_path = g_paste_util_get_history_file_path (name, _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->get_extension (self));
    GPasteSqliteBackendPrivate *priv = g_paste_sqlite_backend_get_priv (self);
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&priv->lock);
    sqlite3 *db = g_paste_sqlite_backend_open (self, db_path);

    /* Keep the (now empty) database so the history is still listed. */
    if (db)
        g_paste_sqlite_backend_exec (db, "DELETE FROM items;");
}

/**************/
/* Management */
/**************/

static void
g_paste_sqlite_backend_delete_history (const GPasteStorageBackend *self,
                                       const gchar                *name,
                                       GError                    **error)
{
    const gchar *extension = _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->get_extension (self);
    g_autofree gchar *db_path = g_paste_util_get_history_file_path (name, extension);
    GPasteSqliteBackendPrivate *priv = g_paste_sqlite_backend_get_priv (self);
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&priv->lock);

    /* Close our connection first so the WAL is checkpointed and its sidecar
     * files can go away with the database. */
    if (priv->db && g_paste_str_equal (priv->db_path, db_path))
    {
        g_clear_pointer (&priv->db, g_paste_sqlite_backend_close);
        g_clear_pointer (&priv->db_path, g_free);
    }

    g_autoptr (GFile) db_file = g_file_new_for_path (db_path);

    g_file_delete (db_file, NULL, error);

    static const gchar *sidecars[] = { "-wal", "-shm" };

    for (guint64 i = 0; i < G_N_ELEMENTS (sidecars); ++i)
    {
        g_autofree gchar *sidecar_path = g_strconcat (db_path, sidecars[i], NULL);
        g_autoptr (GFile) sidecar = g_file_new_for_path (sidecar_path);

        g_file_delete (sidecar, NULL, NULL);
    }
}

static GStrv
g_paste_sqlite_backend_list_histories (const GPasteStorageBackend *self,
                                       GError                    **error)
{
    g_autoptr (GStrvBuilder) history_names = g_strv_builder_new ();
    g_autoptr (GFile) history_dir = g_paste_util_get_history_dir ();
    g_autofree gchar *suffix = g_strconcat (".", _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->get_extension (self), NULL);
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

    while ((history = g_file_enumerator_next_file (histories,
                                                   NULL,
                                                   error)))
    {
        g_autoptr (GFileInfo) h = history;

        if (error && *error)
            return NULL;

        const gchar *raw_name = g_file_info_get_display_name (h);

        if (g_str_has_suffix (raw_name, suffix))
        {
            g_autofree gchar *name = g_strdup (raw_name);

            name[strlen (name) - suffix_len] = '\0';
            g_strv_builder_take (history_names, g_steal_pointer (&name));
        }
    }

    return g_strv_builder_end (history_names);
}

static const gchar *
g_paste_sqlite_backend_get_extension (const GPasteStorageBackend *self G_GNUC_UNUSED)
{
    return "db";
}

static void
g_paste_sqlite_backend_finalize (GObject *object)
{
    GPasteSqliteBackendPrivate *priv = g_paste_sqlite_backend_get_instance_private (G_PASTE_SQLITE_BACKEND (object));

    g_clear_pointer (&priv->db, g_paste_sqlite_backend_close);
    g_clear_pointer (&priv->db_path, g_free);
    g_mutex_clear (&priv->lock);

    G_OBJECT_CLASS (g_paste_sqlite_backend_parent_class)->finalize (object);
}

static void
g_paste_sqlite_backend_class_init (GPasteSqliteBackendClass *klass)
{
    GPasteStorageBackendClass *storage_class = G_PASTE_STORAGE_BACKEND_CLASS (klass);

    storage_class->read_history_file = g_paste_sqlite_backend_read_history_file;
    storage_class->write_history_file = g_paste_sqlite_backend_write_history_file;
    storage_class->get_extension = g_paste_sqlite_backend_get_extension;
    storage_class->delete_history = g_paste_sqlite_backend_delete_history;
    storage_class->list_histories = g_paste_sqlite_backend_list_histories;

    storage_class->add_item = g_paste_sqlite_backend_add_item;
    storage_class->remove_item = g_paste_sqlite_backend_remove_item;
    storage_class->replace_item = g_paste_sqlite_backend_replace_item;
    storage_class->clear_history = g_paste_sqlite_backend_clear_history;

    G_OBJECT_CLASS (klass)->finalize = g_paste_sqlite_backend_finalize;
}

static void
g_paste_sqlite_backend_init (GPasteSqliteBackend *self)
{
    GPasteSqliteBackendPrivate *priv = g_paste_sqlite_backend_get_instance_private (self);

    g_mutex_init (&priv->lock);
}
