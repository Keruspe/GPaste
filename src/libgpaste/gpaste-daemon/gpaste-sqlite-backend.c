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

#ifdef G_PASTE_ENABLE_ENCRYPTION
#define GCR_API_SUBJECT_TO_CHANGE
#include <gcr/gcr.h>

#include <sodium.h>
#endif

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
 * user-readable). Images stay external PNG files, the item value being their
 * path.
 *
 * The encrypted flavor (g_paste_sqlite_backend_new_encrypted, ".dbs" extension)
 * encrypts every content column — items.value, items.name and
 * special_values.data — with crypto_secretbox, each stored blob being
 * nonce ‖ ciphertext. The key is derived from the passphrase with crypto_pwhash
 * (Argon2id, same parameters as the encrypted file backend's stream converter);
 * the random salt, the Argon2 parameters and a key-check secretbox live in a
 * `meta` table so the key can be re-derived and verified — a wrong passphrase
 * is refused like a newer schema, so it can never overwrite the real data with
 * a wrongly-encrypted history. Unlike the plain flavor it persists password
 * items (the content is unreadable without the passphrase), trading the
 * metadata leak of row count/kind/rank/date/checksum for incremental
 * (non-rewriting) updates. */

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

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* When set (in gcr secure memory), the content columns are encrypted, the
     * ".dbs" extension is used, and password entries are persisted rather than
     * skipped. The key is salt-dependent, hence derived per database and cached
     * (also in gcr secure memory) alongside the connection. */
    gchar  *passphrase;
    guchar *key;
#endif
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

#ifdef G_PASTE_ENABLE_ENCRYPTION
/*******************/
/* Encrypted flavor */
/*******************/

#define G_PASTE_SQLITE_KEY_CHECK_MAGIC "GPasteSqliteKeyCheck1"

/* The passphrase, or NULL for a plain database. Safe to call whether or not
 * encryption was built in (see the stub below). */
static const gchar *
g_paste_sqlite_backend_get_passphrase (const GPasteStorageBackend *self)
{
    return g_paste_sqlite_backend_get_priv (self)->passphrase;
}

/* The derived content key of the currently open database, or NULL for the
 * plain flavor. Only valid after a successful open. */
static const guchar *
g_paste_sqlite_backend_get_key (const GPasteStorageBackend *self)
{
    return g_paste_sqlite_backend_get_priv (self)->key;
}

/* Encrypt @length bytes into a freshly allocated nonce ‖ ciphertext blob. */
static guchar *
g_paste_sqlite_backend_encrypt (const guchar *key,
                                gconstpointer data,
                                gsize         length,
                                gsize        *blob_length)
{
    *blob_length = crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES + length;

    guchar *blob = g_malloc (*blob_length);

    randombytes_buf (blob, crypto_secretbox_NONCEBYTES);
    crypto_secretbox_easy (blob + crypto_secretbox_NONCEBYTES, data, length, blob, key);

    return blob;
}

/* Decrypt a nonce ‖ ciphertext blob. The plaintext gets a trailing NUL so the
 * same helper serves text values and raw bytes alike. NULL on a failed
 * authentication (wrong key or corrupted data). */
static guchar *
g_paste_sqlite_backend_decrypt (const guchar *key,
                                gconstpointer blob,
                                gsize         blob_length,
                                gsize        *length)
{
    if (blob_length < crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES)
        return NULL;

    gsize plain_length = blob_length - crypto_secretbox_NONCEBYTES - crypto_secretbox_MACBYTES;
    g_autofree guchar *plain = g_malloc (plain_length + 1);
    const guchar *bytes = blob;

    if (crypto_secretbox_open_easy (plain, bytes + crypto_secretbox_NONCEBYTES,
                                    blob_length - crypto_secretbox_NONCEBYTES, bytes, key) != 0)
        return NULL;

    plain[plain_length] = '\0';
    if (length)
        *length = plain_length;

    return g_steal_pointer (&plain);
}

static gboolean
g_paste_sqlite_backend_derive_key (const gchar  *passphrase,
                                   const guchar *salt,
                                   guint64       opslimit,
                                   guint64       memlimit,
                                   guchar       *key)
{
    return crypto_pwhash (key, crypto_secretbox_KEYBYTES,
                          passphrase, strlen (passphrase),
                          salt, opslimit, memlimit,
                          crypto_pwhash_ALG_ARGON2ID13) == 0;
}

/* Read the salt, Argon2 parameters and key-check blob from the meta table.
 * Quietly returns FALSE when they are absent (fresh database, or no meta table
 * at all), leaving it to the caller to decide what that means. */
static gboolean
g_paste_sqlite_backend_load_crypto_params (sqlite3  *db,
                                           guchar   *salt,
                                           guint64  *opslimit,
                                           guint64  *memlimit,
                                           guchar  **check,
                                           gsize    *check_length)
{
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2 (db, "SELECT key, value FROM meta;", -1, &stmt, NULL) != SQLITE_OK)
        return FALSE;

    gboolean has_salt = FALSE;
    gboolean has_opslimit = FALSE;
    gboolean has_memlimit = FALSE;

    while (sqlite3_step (stmt) == SQLITE_ROW)
    {
        const gchar *key = (const gchar *) sqlite3_column_text (stmt, 0);

        if (g_paste_str_equal (key, "salt") && sqlite3_column_bytes (stmt, 1) == crypto_pwhash_SALTBYTES)
        {
            memcpy (salt, sqlite3_column_blob (stmt, 1), crypto_pwhash_SALTBYTES);
            has_salt = TRUE;
        }
        else if (g_paste_str_equal (key, "opslimit"))
        {
            *opslimit = sqlite3_column_int64 (stmt, 1);
            has_opslimit = TRUE;
        }
        else if (g_paste_str_equal (key, "memlimit"))
        {
            *memlimit = sqlite3_column_int64 (stmt, 1);
            has_memlimit = TRUE;
        }
        else if (g_paste_str_equal (key, "check"))
        {
            *check_length = sqlite3_column_bytes (stmt, 1);
            g_clear_pointer (check, g_free);
            *check = g_memdup2 (sqlite3_column_blob (stmt, 1), *check_length);
        }
    }

    sqlite3_finalize (stmt);

    return has_salt && has_opslimit && has_memlimit && *check;
}

/* Whether @key opens the stored key-check secretbox. */
static gboolean
g_paste_sqlite_backend_key_checks_out (const guchar *key,
                                       const guchar *check,
                                       gsize         check_length)
{
    g_autofree guchar *magic = g_paste_sqlite_backend_decrypt (key, check, check_length, NULL);

    return magic && g_paste_str_equal ((const gchar *) magic, G_PASTE_SQLITE_KEY_CHECK_MAGIC);
}

/* Generate a fresh salt and key check for @key and store them (with the Argon2
 * parameters) in the meta table, replacing any previous ones. */
static gboolean
g_paste_sqlite_backend_store_crypto_params (sqlite3      *db,
                                            const guchar *salt,
                                            guint64       opslimit,
                                            guint64       memlimit,
                                            const guchar *key)
{
    if (!g_paste_sqlite_backend_exec (db, "BEGIN IMMEDIATE; DELETE FROM meta;"))
        return FALSE;

    sqlite3_stmt *stmt = NULL;
    gboolean success = (sqlite3_prepare_v2 (db, "INSERT INTO meta (key, value) VALUES (?, ?);", -1, &stmt, NULL) == SQLITE_OK);

    if (success)
    {
        gsize check_length;
        g_autofree guchar *check = g_paste_sqlite_backend_encrypt (key, G_PASTE_SQLITE_KEY_CHECK_MAGIC,
                                                                   strlen (G_PASTE_SQLITE_KEY_CHECK_MAGIC), &check_length);

        sqlite3_bind_text (stmt, 1, "salt", -1, SQLITE_STATIC);
        sqlite3_bind_blob64 (stmt, 2, salt, crypto_pwhash_SALTBYTES, SQLITE_STATIC);
        success = (sqlite3_step (stmt) == SQLITE_DONE);
        sqlite3_reset (stmt);
        sqlite3_clear_bindings (stmt);

        if (success)
        {
            sqlite3_bind_text (stmt, 1, "opslimit", -1, SQLITE_STATIC);
            sqlite3_bind_int64 (stmt, 2, opslimit);
            success = (sqlite3_step (stmt) == SQLITE_DONE);
            sqlite3_reset (stmt);
            sqlite3_clear_bindings (stmt);
        }

        if (success)
        {
            sqlite3_bind_text (stmt, 1, "memlimit", -1, SQLITE_STATIC);
            sqlite3_bind_int64 (stmt, 2, memlimit);
            success = (sqlite3_step (stmt) == SQLITE_DONE);
            sqlite3_reset (stmt);
            sqlite3_clear_bindings (stmt);
        }

        if (success)
        {
            sqlite3_bind_text (stmt, 1, "check", -1, SQLITE_STATIC);
            sqlite3_bind_blob64 (stmt, 2, check, check_length, SQLITE_TRANSIENT);
            success = (sqlite3_step (stmt) == SQLITE_DONE);
        }

        sqlite3_finalize (stmt);
    }

    if (!success)
        g_warning ("sqlite: failed to store the encryption parameters: %s", sqlite3_errmsg (db));

    return g_paste_sqlite_backend_finish_transaction (db, success);
}

/* Prepare the encrypted flavor on an open database: derive the key from the
 * per-database salt and verify it against the stored key check. A fresh
 * database (or one that is still empty — nothing to lose) gets a new salt and
 * check for this passphrase instead. @wrong_passphrase is set when the
 * passphrase does not unlock existing data, the one failure the caller must
 * report differently. */
static gboolean
g_paste_sqlite_backend_setup_crypto (sqlite3     *db,
                                     const gchar *passphrase,
                                     guchar      *key,
                                     gboolean    *wrong_passphrase)
{
    *wrong_passphrase = FALSE;

    if (!g_paste_sqlite_backend_exec (db, "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value NOT NULL);"))
        return FALSE;

    guchar salt[crypto_pwhash_SALTBYTES];
    guint64 opslimit = 0;
    guint64 memlimit = 0;
    g_autofree guchar *check = NULL;
    gsize check_length = 0;

    if (g_paste_sqlite_backend_load_crypto_params (db, salt, &opslimit, &memlimit, &check, &check_length))
    {
        if (!g_paste_sqlite_backend_derive_key (passphrase, salt, opslimit, memlimit, key))
        {
            g_warning ("sqlite: could not derive the encryption key (out of memory?)");
            return FALSE;
        }

        if (g_paste_sqlite_backend_key_checks_out (key, check, check_length))
            return TRUE;

        /* The wrong passphrase for a history that still holds data must be
         * refused: accepting it would load empty and the next save would
         * destroy the real content. An empty history has nothing to lose, so
         * re-key it for the new passphrase instead of locking the user out. */
        if (g_paste_sqlite_backend_query_int64 (db, "SELECT COUNT (*) FROM items;", 0) > 0)
        {
            *wrong_passphrase = TRUE;
            return FALSE;
        }
    }

    randombytes_buf (salt, sizeof (salt));
    opslimit = crypto_pwhash_OPSLIMIT_MODERATE;
    memlimit = crypto_pwhash_MEMLIMIT_MODERATE;

    if (!g_paste_sqlite_backend_derive_key (passphrase, salt, opslimit, memlimit, key))
    {
        g_warning ("sqlite: could not derive the encryption key (out of memory?)");
        return FALSE;
    }

    return g_paste_sqlite_backend_store_crypto_params (db, salt, opslimit, memlimit, key);
}
#else
static const gchar *
g_paste_sqlite_backend_get_passphrase (const GPasteStorageBackend *self G_GNUC_UNUSED)
{
    return NULL;
}

static const guchar *
g_paste_sqlite_backend_get_key (const GPasteStorageBackend *self G_GNUC_UNUSED)
{
    return NULL;
}
#endif /* G_PASTE_ENABLE_ENCRYPTION */

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
        "    name     TEXT,"             /* Password: reserved for an encrypted variant */
        "    image    BLOB"              /* Image: the encoded PNG */
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

/* Upgrade a database created by an older GPaste (its user_version is @from) to
 * the current schema. There is nothing to migrate yet — the schema has only
 * ever had one version — but the mechanism is here for the first change that
 * needs it: add a `case @from` per historical version, each falling through to
 * the next so any old database upgrades stepwise. */
static gboolean
g_paste_sqlite_backend_migrate_schema (sqlite3 *db,
                                       gint64   from)
{
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
#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* The key is salt-dependent, so it dies with its database's connection. */
    g_clear_pointer (&priv->key, gcr_secure_memory_free);
#endif

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

#ifdef G_PASTE_ENABLE_ENCRYPTION
    if (priv->passphrase)
    {
        guchar *key = gcr_secure_memory_alloc (crypto_secretbox_KEYBYTES);
        gboolean wrong_passphrase = FALSE;

        if (!g_paste_sqlite_backend_setup_crypto (db, priv->passphrase, key, &wrong_passphrase))
        {
            if (wrong_passphrase)
                g_warning ("sqlite: the passphrase does not unlock “%s”; not touching it", db_path);
            gcr_secure_memory_free (key);
            sqlite3_close (db);
            return NULL;
        }

        priv->key = key;
    }
#endif

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

/* Bind @data as-is, or as an encrypted blob when @key is set (the encrypted
 * flavor). Text values go through this too: they are just bytes to bind. */
static void
g_paste_sqlite_backend_bind_content (sqlite3_stmt *stmt,
                                     gint          position,
                                     const guchar *key,
                                     gconstpointer data,
                                     gsize         length)
{
#ifdef G_PASTE_ENABLE_ENCRYPTION
    if (key)
    {
        gsize blob_length;
        g_autofree guchar *blob = g_paste_sqlite_backend_encrypt (key, data, length, &blob_length);

        sqlite3_bind_blob64 (stmt, position, blob, blob_length, SQLITE_TRANSIENT);
        return;
    }
#else
    (void) key;
#endif

    sqlite3_bind_blob64 (stmt, position, data, length, SQLITE_TRANSIENT);
}

/* Like bind_content but for text: a plain database keeps it a readable TEXT
 * column, an encrypted one stores the ciphertext blob. */
static void
g_paste_sqlite_backend_bind_text (sqlite3_stmt *stmt,
                                  gint          position,
                                  const guchar *key,
                                  const gchar  *text)
{
#ifdef G_PASTE_ENABLE_ENCRYPTION
    if (key)
    {
        g_paste_sqlite_backend_bind_content (stmt, position, key, text, strlen (text));
        return;
    }
#else
    (void) key;
#endif

    sqlite3_bind_text (stmt, position, text, -1, SQLITE_TRANSIENT);
}

static gboolean
g_paste_sqlite_backend_write_special_values (sqlite3          *db,
                                             const guchar     *key,
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
        g_paste_sqlite_backend_bind_content (stmt, 4, key, data, data_length);

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
                                               const guchar     *key,
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

    return success && g_paste_sqlite_backend_write_special_values (db, key, item_id, item);
}

/* Bind an image item's PNG for the `image` blob column: from the bytes the
 * item carries, falling back to reading its on-disk cache file for a path-based
 * item that carries none (e.g. imported from the file backend). No bytes
 * anywhere leaves the column NULL. */
static void
g_paste_sqlite_backend_bind_image (sqlite3_stmt          *stmt,
                                   gint                   position,
                                   const guchar          *key,
                                   const GPasteImageItem *image)
{
    GBytes *png = g_paste_image_item_get_png_bytes (image);

    if (png)
    {
        gsize length;
        gconstpointer data = g_bytes_get_data (png, &length);

        g_paste_sqlite_backend_bind_content (stmt, position, key, data, length);
        return;
    }

    g_autofree gchar *data = NULL;
    gsize length = 0;

    if (g_file_get_contents (g_paste_item_get_value (G_PASTE_ITEM ((gpointer) image)), &data, &length, NULL))
        g_paste_sqlite_backend_bind_content (stmt, position, key, data, length);
}

/* Insert @item with @rank, or move the already-stored item with the same uuid
 * to @rank (its value never changes, only its position). Special values are
 * rewritten from the item either way. */
static gboolean
g_paste_sqlite_backend_upsert_item (sqlite3          *db,
                                    const guchar     *key,
                                    const GPasteItem *item,
                                    gint64            rank)
{
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2 (db,
                            "INSERT INTO items (uuid, kind, value, rank, date, checksum, name, image) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
                            "ON CONFLICT (uuid) DO UPDATE SET rank = excluded.rank "
                            "RETURNING id;",
                            -1, &stmt, NULL) != SQLITE_OK)
    {
        g_warning ("sqlite: failed to prepare item insertion: %s", sqlite3_errmsg (db));
        return FALSE;
    }

    sqlite3_bind_text (stmt, 1, g_paste_item_get_uuid (item), -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 2, g_paste_item_get_kind (item), -1, SQLITE_STATIC);
    g_paste_sqlite_backend_bind_text (stmt, 3, key, g_paste_item_get_real_value (item));
    sqlite3_bind_int64 (stmt, 4, rank);

    if (_G_PASTE_IS_IMAGE_ITEM (item))
    {
        const GPasteImageItem *image = _G_PASTE_IMAGE_ITEM (item);
        const gchar *checksum = g_paste_image_item_get_checksum (image);

        sqlite3_bind_int64 (stmt, 5, g_date_time_to_unix ((GDateTime *) g_paste_image_item_get_date (image)));
        if (checksum)
            sqlite3_bind_text (stmt, 6, checksum, -1, SQLITE_STATIC);
        g_paste_sqlite_backend_bind_image (stmt, 8, key, image);
    }
    else if (_G_PASTE_IS_PASSWORD_ITEM (item))
        g_paste_sqlite_backend_bind_text (stmt, 7, key, g_paste_password_item_get_name (_G_PASTE_PASSWORD_ITEM (item)));

    gboolean success = (sqlite3_step (stmt) == SQLITE_ROW);
    gint64 item_id = success ? sqlite3_column_int64 (stmt, 0) : 0;

    if (!success)
        g_warning ("sqlite: failed to write an item: %s", sqlite3_errmsg (db));

    sqlite3_finalize (stmt);

    return success && g_paste_sqlite_backend_rewrite_special_values (db, key, item_id, item);
}

/* Whether @item is persisted at all: password entries only survive in the
 * encrypted flavor (where the content is unreadable without the passphrase),
 * exactly like the plain vs encrypted XML backends. */
static gboolean
g_paste_sqlite_backend_stores_item (const guchar     *key,
                                    const GPasteItem *item)
{
    return key || !g_paste_str_equal (g_paste_item_get_kind (item), "Password");
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

    const guchar *key = g_paste_sqlite_backend_get_key (self);

    /* Count what we'll actually store so the front item gets the highest rank. */
    gint64 rank = 0;

    for (const GList *h = history; h; h = g_list_next (h))
    {
        if (g_paste_sqlite_backend_stores_item (key, h->data))
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

        if (!g_paste_sqlite_backend_stores_item (key, item))
            continue;

        success = g_paste_sqlite_backend_upsert_item (db, key, item, rank--);
    }

    g_paste_sqlite_backend_finish_transaction (db, success);
}

/*****************/
/* Reading items */
/*****************/

/* Read a content column: the column bytes as-is for a plain database, the
 * decrypted plaintext for an encrypted one (with a trailing NUL either way, so
 * the result doubles as a string). NULL when decryption fails. */
static guchar *
g_paste_sqlite_backend_read_content (sqlite3_stmt *stmt,
                                     gint          column,
                                     const guchar *key,
                                     gsize        *length)
{
    gconstpointer blob = sqlite3_column_blob (stmt, column);
    gsize blob_length = sqlite3_column_bytes (stmt, column);

#ifdef G_PASTE_ENABLE_ENCRYPTION
    if (key)
        return g_paste_sqlite_backend_decrypt (key, blob, blob_length, length);
#else
    (void) key;
#endif

    guchar *content = g_malloc (blob_length + 1);

    if (blob_length)
        memcpy (content, blob, blob_length);
    content[blob_length] = '\0';
    if (length)
        *length = blob_length;

    return content;
}

static void
g_paste_sqlite_backend_read_special_values (sqlite3_stmt *stmt,
                                            GEnumClass   *atom_class,
                                            const guchar *key,
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

        gsize length = 0;
        guchar *data = g_paste_sqlite_backend_read_content (stmt, 1, key, &length);

        if (!data)
        {
            g_warning ("sqlite: failed to decrypt a special value; dropping it");
            continue;
        }

        g_paste_item_add_special_value (item, g_paste_binary_data_new (gev->value, g_bytes_new_take (data, length)));
    }

    sqlite3_reset (stmt);
    sqlite3_clear_bindings (stmt);
}

static GPasteItem *
g_paste_sqlite_backend_read_item (sqlite3_stmt *stmt,
                                  const gchar  *history_name,
                                  const guchar *key,
                                  gboolean      images_support)
{
    const gchar *kind = (const gchar *) sqlite3_column_text (stmt, 2);
    g_autofree gchar *value = (gchar *) g_paste_sqlite_backend_read_content (stmt, 3, key, NULL);

    if (!value)
    {
        g_warning ("sqlite: failed to decrypt an item; dropping it");
        return NULL;
    }

    if (g_paste_str_equal (kind, "Text"))
        return g_paste_text_item_new (value);
    if (g_paste_str_equal (kind, "Uris"))
        return g_paste_uris_item_new_from_str (value);
    if (g_paste_str_equal (kind, "Password"))
    {
        g_autofree gchar *name = (gchar *) g_paste_sqlite_backend_read_content (stmt, 6, key, NULL);

        return g_paste_password_item_new (name, value);
    }
    if (g_paste_str_equal (kind, "Color"))
        return g_paste_color_item_new_from_str (value);

    if (g_paste_str_equal (kind, "Image"))
    {
        if (images_support && sqlite3_column_type (stmt, 4) != SQLITE_NULL)
        {
            g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (sqlite3_column_int64 (stmt, 4));
            const gchar *checksum = (const gchar *) sqlite3_column_text (stmt, 5);

            /* The stored blob is the source of truth; the path-based fallback
             * only covers a row whose image could not be materialized (a file
             * import whose cache file had already gone). */
            if (sqlite3_column_type (stmt, 7) != SQLITE_NULL)
            {
                gsize length = 0;
                guchar *data = g_paste_sqlite_backend_read_content (stmt, 7, key, &length);

                if (data)
                {
                    g_autoptr (GBytes) png = g_bytes_new_take (data, length);

                    return g_paste_image_item_new_from_bytes (history_name, png, date, checksum);
                }

                g_warning ("sqlite: failed to decrypt an image; falling back to its cache file");
            }

            return g_paste_image_item_new_from_file (value, date, checksum);
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

    if (sqlite3_prepare_v2 (db, "SELECT id, uuid, kind, value, date, checksum, name, image FROM items ORDER BY rank DESC LIMIT ?;", -1, &stmt, NULL) != SQLITE_OK)
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

    /* The history name (for anchoring blob-loaded images under its own images
     * directory) is the database's basename, extension stripped. */
    g_autofree gchar *basename = g_path_get_basename (history_file_path);
    gchar *dot = strrchr (basename, '.');

    if (dot)
        *dot = '\0';

    GEnumClass *atom_class = g_type_class_ref (G_PASTE_TYPE_SPECIAL_ATOM);
    const guchar *key = g_paste_sqlite_backend_get_key (self);
    gboolean images_support = g_paste_settings_get_images_support (settings);

    while (sqlite3_step (stmt) == SQLITE_ROW)
    {
        GPasteItem *item = g_paste_sqlite_backend_read_item (stmt, basename, key, images_support);

        if (!item)
            continue;

        const gchar *uuid = (const gchar *) sqlite3_column_text (stmt, 1);

        if (uuid && g_uuid_string_is_valid (uuid))
            g_paste_item_set_uuid (item, uuid);

        g_paste_sqlite_backend_read_special_values (sv_stmt, atom_class, key, sqlite3_column_int64 (stmt, 0), item);

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
g_paste_sqlite_backend_reconcile (sqlite3      *db,
                                  const guchar *key,
                                  const GList  *history)
{
    gint64 expected = 0;

    for (const GList *h = history; h; h = g_list_next (h))
    {
        if (g_paste_sqlite_backend_stores_item (key, h->data))
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

        if (g_paste_sqlite_backend_stores_item (key, item))
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

    const guchar *key = g_paste_sqlite_backend_get_key (self);
    gboolean success = TRUE;

    if (g_paste_sqlite_backend_stores_item (key, item))
    {
        gint64 rank = g_paste_sqlite_backend_query_int64 (db, "SELECT COALESCE (MAX (rank), 0) FROM items;", 0) + 1;

        success = g_paste_sqlite_backend_upsert_item (db, key, item, rank);
    }

    if (success)
        g_paste_sqlite_backend_reconcile (db, key, history);

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

    const guchar *key = g_paste_sqlite_backend_get_key (self);

    /* In the plain flavor, an item turning into a password (set_password) must
     * vanish from storage, exactly as the plain XML backend drops passwords on
     * rewrite. The encrypted flavor persists it like any other item. */
    if (!g_paste_sqlite_backend_stores_item (key, item))
    {
        g_clear_pointer (&locker, g_mutex_locker_free);
        g_paste_sqlite_backend_remove_item (self, name, old_uuid);
        return;
    }

    if (!g_paste_sqlite_backend_exec (db, "BEGIN IMMEDIATE;"))
        return;

    sqlite3_stmt *stmt = NULL;
    gboolean success = (sqlite3_prepare_v2 (db,
                                            "UPDATE items SET uuid = ?, kind = ?, value = ?, date = ?, checksum = ?, name = ?, image = ? WHERE uuid = ? "
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
    g_paste_sqlite_backend_bind_text (stmt, 3, key, g_paste_item_get_real_value (item));

    if (_G_PASTE_IS_IMAGE_ITEM (item))
    {
        const GPasteImageItem *image = _G_PASTE_IMAGE_ITEM (item);
        const gchar *checksum = g_paste_image_item_get_checksum (image);

        sqlite3_bind_int64 (stmt, 4, g_date_time_to_unix ((GDateTime *) g_paste_image_item_get_date (image)));
        if (checksum)
            sqlite3_bind_text (stmt, 5, checksum, -1, SQLITE_STATIC);
        g_paste_sqlite_backend_bind_image (stmt, 7, key, image);
    }
    else if (_G_PASTE_IS_PASSWORD_ITEM (item))
        g_paste_sqlite_backend_bind_text (stmt, 6, key, g_paste_password_item_get_name (_G_PASTE_PASSWORD_ITEM (item)));

    sqlite3_bind_text (stmt, 8, old_uuid, -1, SQLITE_STATIC);

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
        success = g_paste_sqlite_backend_rewrite_special_values (db, key, item_id, item);

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
#ifdef G_PASTE_ENABLE_ENCRYPTION
        g_clear_pointer (&priv->key, gcr_secure_memory_free);
#endif
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
g_paste_sqlite_backend_get_extension (const GPasteStorageBackend *self)
{
    /* ".dbs" (s for "secret", like ".xmls") for an encrypted history. */
    return g_paste_sqlite_backend_get_passphrase (self) ? "dbs" : "db";
}

static void
g_paste_sqlite_backend_finalize (GObject *object)
{
    GPasteSqliteBackendPrivate *priv = g_paste_sqlite_backend_get_instance_private (G_PASTE_SQLITE_BACKEND (object));

    g_clear_pointer (&priv->db, g_paste_sqlite_backend_close);
    g_clear_pointer (&priv->db_path, g_free);
    g_mutex_clear (&priv->lock);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_clear_pointer (&priv->key, gcr_secure_memory_free);
    gcr_secure_memory_strfree (priv->passphrase);
#endif

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

#ifdef G_PASTE_ENABLE_ENCRYPTION
/**
 * g_paste_sqlite_backend_new_encrypted:
 * @settings: a #GPasteSettings instance
 * @passphrase: the passphrase the encryption key is derived from
 *
 * Create a SQLite storage backend that encrypts the history's content columns
 * (with the ".dbs" extension) using a key derived from @passphrase. Unlike the
 * plain flavor it persists password entries, since the content is unreadable
 * without the passphrase.
 *
 * Returns: (transfer full) (nullable): a newly allocated #GPasteStorageBackend,
 *          or %NULL if libsodium could not be initialised;
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteStorageBackend *
g_paste_sqlite_backend_new_encrypted (GPasteSettings *settings,
                                      const gchar    *passphrase)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (passphrase && *passphrase, NULL);

    if (sodium_init () < 0)
    {
        g_warning ("Could not initialise libsodium");
        return NULL;
    }

    GPasteStorageBackend *self = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    GPasteSqliteBackendPrivate *priv = g_paste_sqlite_backend_get_instance_private (G_PASTE_SQLITE_BACKEND (self));

    priv->passphrase = gcr_secure_memory_strdup (passphrase);

    return self;
}

/**
 * g_paste_sqlite_backend_passphrase_can_decrypt:
 * @settings: a #GPasteSettings instance
 * @passphrase: the passphrase to check
 *
 * Check whether @passphrase actually unlocks the existing encrypted SQLite
 * history. This guards against accepting a wrong passphrase: the backend would
 * refuse every database and the history would look empty, or worse, an empty
 * one would get re-keyed. Quiet on a mismatch, since re-prompting on a wrong
 * passphrase is an expected flow.
 *
 * Returns: %FALSE only when an encrypted history holding data is present and
 *          @passphrase does not unlock it; %TRUE when it does, or when there is
 *          no real encrypted data on disk to lose
 */
G_PASTE_VISIBLE gboolean
g_paste_sqlite_backend_passphrase_can_decrypt (GPasteSettings *settings,
                                               const gchar    *passphrase)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), FALSE);
    g_return_val_if_fail (passphrase && *passphrase, FALSE);

    if (sodium_init () < 0)
        return FALSE;

    g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, passphrase);
    g_auto (GStrv) names = g_paste_storage_backend_list_histories (backend, NULL);

    for (GStrv name = names; name && *name; ++name)
    {
        g_autofree gchar *path = g_paste_util_get_history_file_path (*name, "dbs");
        sqlite3 *db = NULL;

        /* Read-only: verification must never create or touch anything. */
        if (sqlite3_open_v2 (path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
        {
            sqlite3_close (db);
            continue;
        }

        guchar salt[crypto_pwhash_SALTBYTES];
        guint64 opslimit = 0;
        guint64 memlimit = 0;
        g_autofree guchar *check = NULL;
        gsize check_length = 0;

        /* No crypto parameters (a corrupt or foreign file) means no encrypted
         * data this passphrase could be wrong about. */
        if (!g_paste_sqlite_backend_load_crypto_params (db, salt, &opslimit, &memlimit, &check, &check_length))
        {
            sqlite3_close (db);
            continue;
        }

        guchar key[crypto_secretbox_KEYBYTES];
        gboolean derived = g_paste_sqlite_backend_derive_key (passphrase, salt, opslimit, memlimit, key);
        gboolean checks_out = derived && g_paste_sqlite_backend_key_checks_out (key, check, check_length);
        /* A mismatch only condemns the passphrase when there is data to lose:
         * an empty history gets re-keyed on open instead. */
        gboolean has_data = g_paste_sqlite_backend_query_int64 (db, "SELECT COUNT (*) FROM items;", 0) > 0;

        sodium_memzero (key, sizeof (key));
        sqlite3_close (db);

        /* Keep checking the remaining histories even after a success: with
         * several .dbs keyed differently (one re-keyed while empty, later
         * filled), accepting a passphrase that fails another data-holding
         * history would load that one empty and let its next save overwrite
         * the real content. */
        if (!checks_out && derived && has_data)
            return FALSE;
    }

    return TRUE;
}
#endif
