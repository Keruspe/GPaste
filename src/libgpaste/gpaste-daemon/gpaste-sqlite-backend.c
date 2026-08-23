// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-util.h>

#include <gpaste-daemon/gpaste-color-item.h>
#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-file-backend.h>
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

#include <gpaste-daemon/gpaste-secret-stream-converter.h>
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
 * user-readable). Images are stored as blobs in the `items.image` column, so an
 * item read back from here needs no file on disk; the item value remains its
 * canonical per-history cache path, which is where an image materialized by
 * another flavor lives.
 *
 * The encrypted flavor (g_paste_sqlite_backend_new_encrypted, ".dbs" extension)
 * encrypts every content column — items.value, items.name, items.image and
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

#define G_PASTE_SQLITE_SCHEMA_VERSION 2

/* Far beyond any reachable rank (one increment per add/select), but cheap to
 * guard against: past this, ranks are compacted back to 1..N on open. */
#define G_PASTE_SQLITE_RANK_COMPACT_THRESHOLD (G_GINT64_CONSTANT (1) << 62)

struct _GPasteSqliteBackend
{
    GPasteStorageBackend parent_instance;

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
};

G_PASTE_DEFINE_TYPE (SqliteBackend, sqlite_backend, G_PASTE_TYPE_STORAGE_BACKEND)

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
g_paste_sqlite_backend_finish_transaction (sqlite3 *db,
                                           gboolean success)
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
g_paste_sqlite_backend_get_passphrase (GPasteStorageBackend *self)
{
    return G_PASTE_SQLITE_BACKEND (self)->passphrase;
}

/* The derived content key of the currently open database, or NULL for the
 * plain flavor. Only valid after a successful open. */
static const guchar *
g_paste_sqlite_backend_get_key (GPasteStorageBackend *self)
{
    return G_PASTE_SQLITE_BACKEND (self)->key;
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

/* The parameters come out of the `meta` table, so they are only as trustworthy
 * as the file: g_paste_crypto_derive_key() is what refuses the ones no GPaste
 * ever wrote. */
static gboolean
g_paste_sqlite_backend_derive_key (const gchar  *passphrase,
                                   const guchar *salt,
                                   guint64       opslimit,
                                   guint64       memlimit,
                                   guchar       *key)
{
    g_autoptr (GError) error = NULL;

    if (g_paste_crypto_derive_key (passphrase, strlen (passphrase),
                                   salt, opslimit, memlimit,
                                   key, crypto_secretbox_KEYBYTES, &error))
        return TRUE;

    g_warning ("sqlite: %s", error->message);

    return FALSE;
}

/* Read the salt, Argon2 parameters and key-check blob from the meta table.
 * Quietly returns FALSE when they are absent (fresh database, or no meta table
 * at all), leaving it to the caller to decide what that means. */
static gboolean
g_paste_sqlite_backend_load_crypto_params (sqlite3 *db,
                                           guchar  *salt,
                                           guint64 *opslimit,
                                           guint64 *memlimit,
                                           guchar **check,
                                           gsize   *check_length)
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

/* Store a key check for @key, the salt it was derived from and the Argon2
 * parameters in the meta table, replacing any previous ones. Assumes a
 * transaction is already open, so a caller doing more than this (a re-key, which
 * must swap the parameters and re-encrypt the content as one unit) can share it. */
static gboolean
g_paste_sqlite_backend_write_crypto_params (sqlite3      *db,
                                            const guchar *salt,
                                            guint64       opslimit,
                                            guint64       memlimit,
                                            const guchar *key)
{
    if (!g_paste_sqlite_backend_exec (db, "DELETE FROM meta;"))
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

    return success;
}

/* Generate a fresh salt and key check for @key and store them in their own
 * transaction: what preparing a database for a passphrase needs. */
static gboolean
g_paste_sqlite_backend_store_crypto_params (sqlite3      *db,
                                            const guchar *salt,
                                            guint64       opslimit,
                                            guint64       memlimit,
                                            const guchar *key)
{
    if (!g_paste_sqlite_backend_exec (db, "BEGIN IMMEDIATE;"))
        return FALSE;

    return g_paste_sqlite_backend_finish_transaction (db,
                                                      g_paste_sqlite_backend_write_crypto_params (db, salt, opslimit,
                                                                                                  memlimit, key));
}

/* One collected content blob, waiting to be written back under the new key. */
/* Re-encrypt one content column, row by row: @ids_sql lists the rowids holding
 * a value, then each is fetched through @select_sql, decrypted with @old_key and
 * written back through @update_sql (binding the new blob, then the rowid)
 * encrypted with @new_key. The rowids are taken first so no UPDATE ever runs
 * against the cursor that produced them, and only the rowids are held — one
 * value is in the clear at a time rather than the whole column, and it is wiped
 * as soon as it has been re-encrypted.
 *
 * Assumes the caller's transaction is open. Gives up on anything short of a
 * complete pass: a value that does not decrypt means real corruption (the key
 * check passed), and a scan cut short — a busy database, an I/O error — must not
 * be mistaken for the end of the column, or the key check would be swapped while
 * part of it is still under the old key, which no passphrase would then open. */
static gboolean
g_paste_sqlite_backend_rekey_column (sqlite3      *db,
                                     const guchar *old_key,
                                     const guchar *new_key,
                                     const gchar  *ids_sql,
                                     const gchar  *select_sql,
                                     const gchar  *update_sql)
{
    g_autoptr (GArray) rowids = g_array_new (FALSE, FALSE, sizeof (gint64));
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2 (db, ids_sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        g_warning ("sqlite: failed to prepare \u201c%s\u201d: %s", ids_sql, sqlite3_errmsg (db));
        return FALSE;
    }

    gint rc;

    while ((rc = sqlite3_step (stmt)) == SQLITE_ROW)
    {
        gint64 rowid = sqlite3_column_int64 (stmt, 0);

        g_array_append_val (rowids, rowid);
    }

    sqlite3_finalize (stmt);

    if (rc != SQLITE_DONE)
    {
        g_warning ("sqlite: could not read every row of \u201c%s\u201d: %s", ids_sql, sqlite3_errmsg (db));
        return FALSE;
    }

    sqlite3_stmt *select = NULL;
    sqlite3_stmt *update = NULL;
    gboolean success = (sqlite3_prepare_v2 (db, select_sql, -1, &select, NULL) == SQLITE_OK &&
                        sqlite3_prepare_v2 (db, update_sql, -1, &update, NULL) == SQLITE_OK);

    if (!success)
        g_warning ("sqlite: failed to prepare the re-encryption of \u201c%s\u201d: %s", select_sql, sqlite3_errmsg (db));

    for (guint i = 0; success && i < rowids->len; ++i)
    {
        gint64 rowid = g_array_index (rowids, gint64, i);

        sqlite3_bind_int64 (select, 1, rowid);

        if (sqlite3_step (select) != SQLITE_ROW)
        {
            g_warning ("sqlite: a value went missing while re-encrypting it: %s", sqlite3_errmsg (db));
            success = FALSE;
        }
        else
        {
            gsize length = 0;
            g_autofree guchar *plain = g_paste_sqlite_backend_decrypt (old_key,
                                                                       sqlite3_column_blob (select, 0),
                                                                       sqlite3_column_bytes (select, 0),
                                                                       &length);

            if (!plain)
            {
                g_warning ("sqlite: a stored value did not decrypt; leaving the passphrase alone");
                success = FALSE;
            }
            else
            {
                gsize blob_length;
                g_autofree guchar *blob = g_paste_sqlite_backend_encrypt (new_key, plain, length, &blob_length);

                /* Item contents, passwords included: do not leave them behind in
                 * freed heap now that they are stored again. */
                sodium_memzero (plain, length);

                sqlite3_bind_blob64 (update, 1, blob, blob_length, SQLITE_STATIC);
                sqlite3_bind_int64 (update, 2, rowid);

                if (sqlite3_step (update) != SQLITE_DONE)
                {
                    g_warning ("sqlite: failed to re-encrypt a stored value: %s", sqlite3_errmsg (db));
                    success = FALSE;
                }

                sqlite3_reset (update);
                sqlite3_clear_bindings (update);
            }
        }

        sqlite3_reset (select);
        sqlite3_clear_bindings (select);
    }

    sqlite3_finalize (select);
    sqlite3_finalize (update);

    return success;
}

/* Every content column, as the (select, update) pair
 * g_paste_sqlite_backend_rekey_column() drives. A column added to the schema
 * that holds user content has to be listed here too, or a passphrase change
 * would leave it behind, unreadable. */
static const struct
{
    const gchar *ids_sql;
    const gchar *select_sql;
    const gchar *update_sql;
} g_paste_sqlite_backend_content_columns[] = {
    { "SELECT rowid FROM items;",
      "SELECT value FROM items WHERE rowid = ?;",
      "UPDATE items SET value = ? WHERE rowid = ?;" },
    { "SELECT rowid FROM items WHERE name IS NOT NULL;",
      "SELECT name FROM items WHERE rowid = ?;",
      "UPDATE items SET name = ? WHERE rowid = ?;" },
    { "SELECT rowid FROM items WHERE image IS NOT NULL;",
      "SELECT image FROM items WHERE rowid = ?;",
      "UPDATE items SET image = ? WHERE rowid = ?;" },
    { "SELECT rowid FROM special_values;",
      "SELECT data FROM special_values WHERE rowid = ?;",
      "UPDATE special_values SET data = ? WHERE rowid = ?;" },
};

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
        /* derive_key() has already said why. */
        if (!g_paste_sqlite_backend_derive_key (passphrase, salt, opslimit, memlimit, key))
            return FALSE;

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
g_paste_sqlite_backend_get_passphrase (GPasteStorageBackend *self G_GNUC_UNUSED)
{
    return NULL;
}

static const guchar *
g_paste_sqlite_backend_get_key (GPasteStorageBackend *self G_GNUC_UNUSED)
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
        "    image    BLOB,"             /* Image: the encoded PNG */
        "    favourite INTEGER NOT NULL DEFAULT 0" /* pinned: exempt from both caps */
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
 * the current schema. One `case @from` per historical version, each falling
 * through to the next so any old database upgrades stepwise. */
static gboolean
g_paste_sqlite_backend_migrate_schema (sqlite3 *db,
                                       gint64   from)
{
    switch (from)
    {
    case 1:
        /* Favourites. The default is what makes this a pure addition: every
         * item an older GPaste stored comes back unpinned, which is what it
         * was. */
        if (!g_paste_sqlite_backend_exec (db, "ALTER TABLE items ADD COLUMN favourite INTEGER NOT NULL DEFAULT 0;"))
            return FALSE;
        G_GNUC_FALLTHROUGH;
    default:
        return TRUE;
    }
}

/* Get the (cached) connection for @db_path, opening and preparing the database
 * as needed. Returns NULL (and warns) when the database cannot be used, e.g.
 * when it was created by a newer GPaste: every operation then no-ops instead
 * of risking the data. Must be called with the backend lock held. */
static sqlite3 *
g_paste_sqlite_backend_open (GPasteStorageBackend *self,
                             const gchar          *db_path)
{
    GPasteSqliteBackend *backend = G_PASTE_SQLITE_BACKEND (self);

    if (backend->db && g_paste_str_equal (backend->db_path, db_path))
        return backend->db;

    g_clear_pointer (&backend->db, g_paste_sqlite_backend_close);
    g_clear_pointer (&backend->db_path, g_free);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* The key is salt-dependent, so it dies with its database's connection. */
    g_clear_pointer (&backend->key, gcr_secure_memory_free);
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
    if (backend->passphrase)
    {
        guchar *key = gcr_secure_memory_alloc (crypto_secretbox_KEYBYTES);
        gboolean wrong_passphrase = FALSE;

        if (!g_paste_sqlite_backend_setup_crypto (db, backend->passphrase, key, &wrong_passphrase))
        {
            if (wrong_passphrase)
                g_warning ("sqlite: the passphrase does not unlock “%s”; not touching it", db_path);
            gcr_secure_memory_free (key);
            sqlite3_close (db);
            return NULL;
        }

        backend->key = key;
    }
#endif

    if (g_paste_sqlite_backend_query_int64 (db, "SELECT COALESCE (MAX (rank), 0) FROM items;", 0) > G_PASTE_SQLITE_RANK_COMPACT_THRESHOLD)
    {
        g_paste_sqlite_backend_exec (db,
                                     "UPDATE items SET rank = ranked.new_rank "
                                     "FROM (SELECT id, ROW_NUMBER () OVER (ORDER BY rank) AS new_rank FROM items) AS ranked "
                                     "WHERE items.id = ranked.id;");
    }

    backend->db = db;
    backend->db_path = g_strdup (db_path);

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
g_paste_sqlite_backend_write_special_values (sqlite3      *db,
                                             const guchar *key,
                                             gint64        item_id,
                                             GPasteItem   *item)
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
        GPasteBinaryData *value = val->data;
        GEnumValue *gev = g_enum_get_value (atom_class, g_paste_binary_data_get_mime (value));

        /* Skip a value carrying an unknown atom rather than dereferencing NULL,
         * mirroring the guarded read path in read_special_values. */
        if (!gev)
        {
            g_warning ("sqlite: unknown mime: %d", g_paste_binary_data_get_mime (value));
            continue;
        }

        const gchar *mime = gev->value_nick;
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

/* Bind an image item's PNG for the `image` blob column: from the bytes the
 * item carries, falling back to the file it was read from for an item that
 * carries none (e.g. imported from the file backend). No bytes anywhere leaves
 * the column NULL. */
static void
g_paste_sqlite_backend_bind_image (sqlite3_stmt    *stmt,
                                   gint             position,
                                   const guchar    *key,
                                   GPasteImageItem *image)
{
    GBytes *png = g_paste_image_item_get_png_bytes (image);

    if (png)
    {
        gsize length;
        gconstpointer data = g_bytes_get_data (png, &length);

        g_paste_sqlite_backend_bind_content (stmt, position, key, data, length);
        return;
    }

    const gchar *cache_path = g_paste_image_item_get_cache_path (image);
    g_autofree gchar *data = NULL;
    gsize length = 0;

    /* Best effort, like the file backend's own materialization: an item whose
     * cache file has gone -- or that never had one -- is stored without its
     * image rather than not at all, and the column simply stays NULL. */
    if (cache_path && g_file_get_contents (cache_path, &data, &length, NULL))
        g_paste_sqlite_backend_bind_content (stmt, position, key, data, length);
}

/* Replace an item's stored special values with the ones it carries. */
static gboolean
g_paste_sqlite_backend_rewrite_special_values (sqlite3      *db,
                                               const guchar *key,
                                               gint64        item_id,
                                               GPasteItem   *item)
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

/* Bind an item's content columns: uuid, kind and value at the fixed positions
 * 1-3, then the image (date, checksum, image blob) or password (name) columns
 * starting at @meta_base, with the favourite flag closing the group at
 * @meta_base + 4. The INSERT and UPDATE statements share this layout and
 * differ only by @meta_base — the INSERT carries an extra rank column between the
 * value and the meta group, so it binds at base 5 while the UPDATE binds at 4.
 * Keeping the two in one place stops their column indices drifting apart. */
static void
g_paste_sqlite_backend_bind_item (sqlite3_stmt *stmt,
                                  const guchar *key,
                                  GPasteItem   *item,
                                  gint          meta_base)
{
    sqlite3_bind_text (stmt, 1, g_paste_item_get_uuid (item), -1, SQLITE_STATIC);
    /* The nick is a static string owned by the enum class, hence SQLITE_STATIC. */
    sqlite3_bind_text (stmt, 2, g_paste_item_kind_to_string (g_paste_item_get_kind (item)), -1, SQLITE_STATIC);
    g_paste_sqlite_backend_bind_text (stmt, 3, key, g_paste_item_get_real_value (item));
    /* Every kind has one, so it is bound outside the per-kind branches below.
     * Plaintext like rank and date: the read has to order and filter on it. */
    sqlite3_bind_int (stmt, meta_base + 4, g_paste_item_is_favourite (item));

    if (G_PASTE_IS_IMAGE_ITEM (item))
    {
        GPasteImageItem *image = G_PASTE_IMAGE_ITEM (item);
        const gchar *checksum = g_paste_image_item_get_checksum (image);

        sqlite3_bind_int64 (stmt, meta_base, g_date_time_to_unix ((GDateTime *) g_paste_image_item_get_date (image)));
        if (checksum)
            sqlite3_bind_text (stmt, meta_base + 1, checksum, -1, SQLITE_STATIC);
        g_paste_sqlite_backend_bind_image (stmt, meta_base + 3, key, image);
    }
    else if (G_PASTE_IS_PASSWORD_ITEM (item))
        g_paste_sqlite_backend_bind_text (stmt, meta_base + 2, key, g_paste_password_item_get_name (G_PASTE_PASSWORD_ITEM (item)));
}

/* Insert @item with @rank, or move the already-stored item with the same uuid
 * to @rank (its value never changes, only its position and whether it is
 * pinned). Special values are rewritten from the item either way. */
static gboolean
g_paste_sqlite_backend_upsert_item (sqlite3      *db,
                                    const guchar *key,
                                    GPasteItem   *item,
                                    gint64        rank)
{
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2 (db,
                            "INSERT INTO items (uuid, kind, value, rank, date, checksum, name, image, favourite) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
                            "ON CONFLICT (uuid) DO UPDATE SET rank = excluded.rank, favourite = excluded.favourite "
                            "RETURNING id;",
                            -1, &stmt, NULL) != SQLITE_OK)
    {
        g_warning ("sqlite: failed to prepare item insertion: %s", sqlite3_errmsg (db));
        return FALSE;
    }

    /* uuid, kind, value at 1-3, then rank at 4, then the meta group at base 5. */
    g_paste_sqlite_backend_bind_item (stmt, key, item, 5);
    sqlite3_bind_int64 (stmt, 4, rank);

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
g_paste_sqlite_backend_stores_item (const guchar *key,
                                    GPasteItem   *item)
{
    return key || g_paste_item_get_kind (item) != G_PASTE_ITEM_KIND_PASSWORD;
}

/* How many of @history's items this backend actually persists. Used to rank a
 * full rewrite (front item gets the highest rank) and to tell whether the store
 * has drifted from the snapshot during reconcile — one definition so the two can
 * never disagree on which items count. */
static gint64
g_paste_sqlite_backend_count_stored (const guchar *key,
                                     const GList  *history)
{
    gint64 count = 0;

    for (const GList *h = history; h; h = g_list_next (h))
    {
        if (g_paste_sqlite_backend_stores_item (key, h->data))
            ++count;
    }

    return count;
}

static void
g_paste_sqlite_backend_write_history_file (GPasteStorageBackend *self,
                                           const gchar          *name,
                                           const GList          *history)
{
    GPasteSqliteBackend *backend = G_PASTE_SQLITE_BACKEND (self);
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&backend->lock);
    g_autofree gchar *history_file_path = g_paste_storage_backend_get_history_file_path (self, name);
    sqlite3 *db = g_paste_sqlite_backend_open (self, history_file_path);

    if (!db)
        return;

    const guchar *key = g_paste_sqlite_backend_get_key (self);

    /* Count what we'll actually store so the front item gets the highest rank. */
    gint64 rank = g_paste_sqlite_backend_count_stored (key, history);

    /* One transaction, so replacing the whole content is atomic: a failure
     * (or crash) rolls back to the previous state instead of losing data. */
    if (!g_paste_sqlite_backend_exec (db, "BEGIN IMMEDIATE;"))
        return;

    gboolean success = g_paste_sqlite_backend_exec (db, "DELETE FROM items;");

    for (const GList *h = history; success && h; h = g_list_next (h))
    {
        GPasteItem *item = h->data;

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
                                  const guchar *key,
                                  gboolean      images_support)
{
    const gchar *kind_str = (const gchar *) sqlite3_column_text (stmt, 2);
    GPasteItemKind kind = g_paste_item_kind_from_string (kind_str);
    /* A password's value comes back through here too, so the buffer is wiped
     * rather than merely freed once the item has taken a copy of it. */
    g_autoptr (GPasteCleartext) value = (gchar *) g_paste_sqlite_backend_read_content (stmt, 3, key, NULL);

    if (!value)
    {
        g_warning ("sqlite: failed to decrypt an item; dropping it");
        return NULL;
    }

    switch (kind)
    {
    case G_PASTE_ITEM_KIND_TEXT:
        return g_paste_text_item_new (value);
    case G_PASTE_ITEM_KIND_URIS:
        return g_paste_uris_item_new_from_str (value);
    case G_PASTE_ITEM_KIND_PASSWORD:
    {
        g_autoptr (GPasteCleartext) name = (gchar *) g_paste_sqlite_backend_read_content (stmt, 6, key, NULL);

        return g_paste_password_item_new (name, value);
    }
    case G_PASTE_ITEM_KIND_COLOR:
        return g_paste_color_item_new_from_str (value);
    case G_PASTE_ITEM_KIND_IMAGE:
    {
        if (images_support && sqlite3_column_type (stmt, 4) != SQLITE_NULL)
        {
            g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (sqlite3_column_int64 (stmt, 4));
            const gchar *checksum = (const gchar *) sqlite3_column_text (stmt, 5);

            /* The stored blob is the source of truth, and an item built from
             * one holds no file: nothing on disk backs a row. The path-based
             * fallback only covers a row whose image was never turned into a
             * blob (a file import whose cache file had already gone), and whose
             * value is therefore still the path it was imported with. */
            if (sqlite3_column_type (stmt, 7) != SQLITE_NULL)
            {
                gsize length = 0;
                guchar *data = g_paste_sqlite_backend_read_content (stmt, 7, key, &length);

                if (data)
                {
                    g_autoptr (GBytes) png = g_bytes_new_take (data, length);

                    return g_paste_image_item_new_from_bytes (png, date, checksum);
                }

                g_warning ("sqlite: failed to decrypt an image; falling back to its cache file");
            }

            return g_paste_image_item_new_from_file (value, date, checksum);
        }

        /* Images are disabled (or the row carries no date): drop whatever an
         * older, file-backed life left on disk. Only such a row names a file at
         * all — one written here stores its image as a blob and its value as
         * the checksum, which is nobody's path. */
        if (g_path_is_absolute (value))
            g_paste_file_backend_delete_image (value);

        return NULL;
    }
    case G_PASTE_ITEM_KIND_INVALID:
        break;
    }

    g_warning ("Unknown item kind: %s", kind_str);

    return NULL;
}

/* What read_history_file would have returned, counted in the database rather
 * than carried out of it: same predicate, same cap, same favourites, and the
 * same image rows dropped when images are turned off.
 *
 * On a connection of its own, closed again on the way out, rather than the
 * cached one: counting needs no content and so no key, while opening another
 * database through the cache would evict the current history's connection --
 * and with it the derived key, which only a fresh Argon2 pass can replace. That
 * matters here because listing counts every history in turn.
 *
 * Read-only, so a history that is not there is not created by being counted; it
 * simply counts 0, as an unreadable one does. A row whose content cannot be
 * decoded at all is still counted, unlike in a read, which drops it -- that
 * is corruption, and a listing one item out is the smaller of the two problems
 * it causes.
 *
 * Opening by hand means the gates g_paste_sqlite_backend_open () applies have to
 * be applied here too, or a listing answers for a database a read refuses. A
 * user_version from a newer GPaste counts 0, the way that read reports nothing;
 * the schema the version names picks the query, since a database still on v1 has
 * no favourite column and a count that assumed one would answer 0 for a history
 * that reads back in full. The passphrase is the one gate it cannot apply: the
 * key is per-database and only an Argon2 pass produces it, which a listing
 * cannot afford once per history, so a `.dbs` written under some other
 * passphrase is counted here and refused on the read. */
static gsize
g_paste_sqlite_backend_count_history (GPasteStorageBackend *self,
                                      const gchar          *name)
{
    GPasteSettings *settings = g_paste_storage_backend_get_settings (self);
    GPasteSqliteBackend *backend = G_PASTE_SQLITE_BACKEND (self);
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&backend->lock);
    g_autofree gchar *history_file_path = g_paste_storage_backend_get_history_file_path (self, name);
    sqlite3 *db = NULL;

    if (sqlite3_open_v2 (history_file_path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
    {
        sqlite3_close (db);

        return 0;
    }

    sqlite3_busy_timeout (db, 5000);

    gint64 version = g_paste_sqlite_backend_query_int64 (db, "PRAGMA user_version;", 0);

    /* 0 is a database with no schema yet, which a read creates and finds empty;
     * anything past ours is one this GPaste will not touch. */
    if (!version || version > G_PASTE_SQLITE_SCHEMA_VERSION)
    {
        sqlite3_close (db);

        return 0;
    }

    gboolean images_support = g_paste_settings_get_images_support (settings);
    const gchar *query;

    if (version >= 2)
    {
        query = (images_support)
            ? "SELECT COUNT (*) FROM items "
              "WHERE favourite = 1 "
              "   OR id IN (SELECT id FROM items WHERE favourite = 0 ORDER BY rank DESC LIMIT ?1);"
            : "SELECT COUNT (*) FROM items "
              "WHERE kind != ?2 "
              "  AND (favourite = 1 "
              "       OR id IN (SELECT id FROM items WHERE favourite = 0 ORDER BY rank DESC LIMIT ?1));";
    }
    else
    {
        /* v1 has no favourite column, and the migration that adds one defaults
         * every row to unpinned: what a read of this database returns is the cap
         * applied to all of it. */
        query = (images_support)
            ? "SELECT COUNT (*) FROM items "
              "WHERE id IN (SELECT id FROM items ORDER BY rank DESC LIMIT ?1);"
            : "SELECT COUNT (*) FROM items "
              "WHERE kind != ?2 "
              "  AND id IN (SELECT id FROM items ORDER BY rank DESC LIMIT ?1);";
    }

    sqlite3_stmt *stmt = NULL;
    gsize count = 0;

    if (sqlite3_prepare_v2 (db, query, -1, &stmt, NULL) != SQLITE_OK)
    {
        g_warning ("sqlite: failed to prepare history count query: %s", sqlite3_errmsg (db));
        sqlite3_close (db);

        return 0;
    }

    sqlite3_bind_int64 (stmt, 1, g_paste_settings_get_max_history_size (settings));

    /* The nick the rows were written with, not a literal: it is one enum
     * registration away from being something else. */
    if (!images_support)
        sqlite3_bind_text (stmt, 2, g_paste_item_kind_to_string (G_PASTE_ITEM_KIND_IMAGE), -1, SQLITE_STATIC);

    if (sqlite3_step (stmt) == SQLITE_ROW)
        count = sqlite3_column_int64 (stmt, 0);

    sqlite3_finalize (stmt);
    sqlite3_close (db);

    return count;
}

static gboolean
g_paste_sqlite_backend_read_history_file (GPasteStorageBackend *self,
                                          const gchar          *name,
                                          GList               **history,
                                          gsize                *size)
{
    GPasteSettings *settings = g_paste_storage_backend_get_settings (self);
    GPasteSqliteBackend *backend = G_PASTE_SQLITE_BACKEND (self);
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&backend->lock);
    g_autofree gchar *history_file_path = g_paste_storage_backend_get_history_file_path (self, name);
    /* Opening creates the database on first read, so a fresh history shows up
     * in listings just like the file backend's empty placeholder. */
    sqlite3 *db = g_paste_sqlite_backend_open (self, history_file_path);

    *history = NULL;
    *size = 0;

    /* A NULL db means the (possibly encrypted) database is present but could not
     * be opened/decrypted: report the failure so a caller never mistakes it for
     * a genuinely empty history. */
    if (!db)
        return FALSE;

    sqlite3_stmt *stmt = NULL;

    /* The LIMIT applies to the items the size cap can actually evict: a
     * favourite is read back however deep it has sunk, or the next save would
     * destroy the very thing pinning it was meant to protect. */
    if (sqlite3_prepare_v2 (db,
                            "SELECT id, uuid, kind, value, date, checksum, name, image, favourite FROM items "
                            "WHERE favourite = 1 "
                            "   OR id IN (SELECT id FROM items WHERE favourite = 0 ORDER BY rank DESC LIMIT ?) "
                            "ORDER BY rank DESC;",
                            -1, &stmt, NULL) != SQLITE_OK)
    {
        g_warning ("sqlite: failed to prepare history query: %s", sqlite3_errmsg (db));
        return FALSE;
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
        return FALSE;
    }

    GEnumClass *atom_class = g_type_class_ref (G_PASTE_TYPE_SPECIAL_ATOM);
    const guchar *key = g_paste_sqlite_backend_get_key (self);
    gboolean images_support = g_paste_settings_get_images_support (settings);

    while (sqlite3_step (stmt) == SQLITE_ROW)
    {
        GPasteItem *item = g_paste_sqlite_backend_read_item (stmt, key, images_support);

        if (!item)
            continue;

        const gchar *uuid = (const gchar *) sqlite3_column_text (stmt, 1);

        if (uuid && g_uuid_string_is_valid (uuid))
            g_paste_item_set_uuid (item, uuid);

        g_paste_item_set_favourite (item, sqlite3_column_int (stmt, 8));

        g_paste_sqlite_backend_read_special_values (sv_stmt, atom_class, key, sqlite3_column_int64 (stmt, 0), item);

        *history = g_list_prepend (*history, item);
        *size += g_paste_item_get_size (item);
    }

    g_type_class_unref (atom_class);
    sqlite3_finalize (sv_stmt);
    sqlite3_finalize (stmt);

    *history = g_list_reverse (*history);

    return TRUE;
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
    gint64 expected = g_paste_sqlite_backend_count_stored (key, history);

    /* The common case: nothing rode along, the store already matches. Only
     * build the uuid set (and scan the table) when it actually does not. */
    if (g_paste_sqlite_backend_query_int64 (db, "SELECT COUNT (*) FROM items;", expected) == expected)
        return;

    g_autoptr (GHashTable) uuids = g_hash_table_new (g_str_hash, g_str_equal);

    for (const GList *h = history; h; h = g_list_next (h))
    {
        GPasteItem *item = h->data;

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
g_paste_sqlite_backend_add_item (GPasteStorageBackend *self,
                                 const gchar          *name,
                                 GPasteItem           *item,
                                 const GList          *history)
{
    g_autofree gchar *db_path = g_paste_storage_backend_get_history_file_path (self, name);
    GPasteSqliteBackend *backend = G_PASTE_SQLITE_BACKEND (self);
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&backend->lock);
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

    /* A %NULL history says nothing was displaced by this add, so no row can
     * have been orphaned and there is nothing to reconcile. */
    if (success && history)
        g_paste_sqlite_backend_reconcile (db, key, history);

    g_paste_sqlite_backend_finish_transaction (db, success);
}

static void
g_paste_sqlite_backend_remove_item (GPasteStorageBackend *self,
                                    const gchar          *name,
                                    const gchar          *uuid)
{
    g_autofree gchar *db_path = g_paste_storage_backend_get_history_file_path (self, name);
    GPasteSqliteBackend *backend = G_PASTE_SQLITE_BACKEND (self);
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&backend->lock);
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
g_paste_sqlite_backend_replace_item (GPasteStorageBackend *self,
                                     const gchar          *name,
                                     const gchar          *old_uuid,
                                     GPasteItem           *item)
{
    g_autofree gchar *db_path = g_paste_storage_backend_get_history_file_path (self, name);
    GPasteSqliteBackend *backend = G_PASTE_SQLITE_BACKEND (self);
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&backend->lock);
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
                                            "UPDATE items SET uuid = ?, kind = ?, value = ?, date = ?, checksum = ?, name = ?, image = ?, favourite = ? WHERE uuid = ? "
                                            "RETURNING id;",
                                            -1, &stmt, NULL) == SQLITE_OK);

    if (!success)
    {
        g_warning ("sqlite: failed to prepare item replacement: %s", sqlite3_errmsg (db));
        g_paste_sqlite_backend_exec (db, "ROLLBACK;");
        return;
    }

    /* uuid, kind, value at 1-3, the meta group at base 4, then old uuid at 9. */
    g_paste_sqlite_backend_bind_item (stmt, key, item, 4);
    sqlite3_bind_text (stmt, 9, old_uuid, -1, SQLITE_STATIC);

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
g_paste_sqlite_backend_clear_history (GPasteStorageBackend *self,
                                      const gchar          *name)
{
    g_autofree gchar *db_path = g_paste_storage_backend_get_history_file_path (self, name);
    GPasteSqliteBackend *backend = G_PASTE_SQLITE_BACKEND (self);
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&backend->lock);
    sqlite3 *db = g_paste_sqlite_backend_open (self, db_path);

    /* Keep the (now empty) database so the history is still listed. */
    if (db)
        g_paste_sqlite_backend_exec (db, "DELETE FROM items;");
}

/**************/
/* Management */
/**************/

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* Re-encrypt @name under @new_passphrase. Everything — every content column and
 * the meta parameters the key is checked against — moves in a single
 * transaction, so the database is only ever readable with the old passphrase or
 * with the new one, never stuck between them. */
static gboolean
g_paste_sqlite_backend_rekey (GPasteStorageBackend *self,
                              const gchar          *name,
                              const gchar          *new_passphrase)
{
    if (!g_paste_sqlite_backend_get_passphrase (self))
    {
        g_warning ("This history is not encrypted: it has no passphrase to change");
        return FALSE;
    }

    g_autofree gchar *db_path = g_paste_storage_backend_get_history_file_path (self, name);
    GPasteSqliteBackend *backend = G_PASTE_SQLITE_BACKEND (self);
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&backend->lock);
    /* Opening verifies the current passphrase against the stored key check, so
     * by here backend->key is the one everything is encrypted with. */
    sqlite3 *db = g_paste_sqlite_backend_open (self, db_path);

    if (!db)
        return FALSE;

    guchar salt[crypto_pwhash_SALTBYTES];
    guint64 opslimit = crypto_pwhash_OPSLIMIT_MODERATE;
    guint64 memlimit = crypto_pwhash_MEMLIMIT_MODERATE;
    guchar *new_key = gcr_secure_memory_alloc (crypto_secretbox_KEYBYTES);

    randombytes_buf (salt, sizeof (salt));

    if (!g_paste_sqlite_backend_derive_key (new_passphrase, salt, opslimit, memlimit, new_key))
    {
        g_warning ("sqlite: could not derive the new encryption key (out of memory?)");
        gcr_secure_memory_free (new_key);

        return FALSE;
    }

    if (!g_paste_sqlite_backend_exec (db, "BEGIN IMMEDIATE;"))
    {
        gcr_secure_memory_free (new_key);

        return FALSE;
    }

    gboolean success = TRUE;

    for (guint64 i = 0; success && i < G_N_ELEMENTS (g_paste_sqlite_backend_content_columns); ++i)
    {
        success = g_paste_sqlite_backend_rekey_column (db, backend->key, new_key,
                                                       g_paste_sqlite_backend_content_columns[i].ids_sql,
                                                       g_paste_sqlite_backend_content_columns[i].select_sql,
                                                       g_paste_sqlite_backend_content_columns[i].update_sql);
    }

    /* The new salt and key check land in the same transaction as the content
     * they describe: rolling back takes them with it. */
    success = success && g_paste_sqlite_backend_write_crypto_params (db, salt, opslimit, memlimit, new_key);
    success = g_paste_sqlite_backend_finish_transaction (db, success);

    if (!success)
    {
        gcr_secure_memory_free (new_key);
        g_warning ("Failed to re-encrypt the history \"%s\"; it keeps its current passphrase", name);

        return FALSE;
    }

    /* Committed. This instance deliberately stays keyed to the passphrase it was
     * built with: a passphrase change re-keys every history through one backend,
     * so adopting the new one here would make the next history — still encrypted
     * with the old one — fail to open. Only the cached connection has to go, its
     * key no longer matching what is now on disk; re-opening this database
     * through this instance then refuses it at the key check rather than reading
     * it with a stale key. */
    g_clear_pointer (&backend->db, g_paste_sqlite_backend_close);
    g_clear_pointer (&backend->db_path, g_free);
    g_clear_pointer (&backend->key, gcr_secure_memory_free);
    gcr_secure_memory_free (new_key);

    return TRUE;
}
#endif /* G_PASTE_ENABLE_ENCRYPTION */

static void
g_paste_sqlite_backend_delete_history (GPasteStorageBackend *self,
                                       const gchar          *name,
                                       GError              **error)
{
    const gchar *extension = g_paste_storage_backend_get_extension (self);
    g_autofree gchar *db_path = g_paste_util_get_history_file_path (name, extension);
    GPasteSqliteBackend *backend = G_PASTE_SQLITE_BACKEND (self);
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&backend->lock);

    /* Close our connection first so the WAL is checkpointed and its sidecar
     * files can go away with the database. */
    if (backend->db && g_paste_str_equal (backend->db_path, db_path))
    {
        g_clear_pointer (&backend->db, g_paste_sqlite_backend_close);
        g_clear_pointer (&backend->db_path, g_free);
#ifdef G_PASTE_ENABLE_ENCRYPTION
        g_clear_pointer (&backend->key, gcr_secure_memory_free);
#endif
    }

    g_autoptr (GFile) db_file = g_file_new_for_path (db_path);

    g_file_delete (db_file, NULL, error);

    static const gchar *sidecars[] = { "-wal", "-shm" };

    for (guint64 i = 0; i < G_N_ELEMENTS (sidecars); ++i)
    {
        g_autofree gchar *sidecar_path = g_strconcat (db_path, sidecars[i], NULL);
        g_autoptr (GFile) sidecar = g_file_new_for_path (sidecar_path);

        /* Only present while a connection is (or was) open, so a missing one is
         * the normal case and not worth reporting -- the database itself is the
         * delete that counts, and it reports through @error above. */
        g_file_delete (sidecar, NULL, NULL);
    }
}

static GPasteStorage
g_paste_sqlite_backend_get_kind (GPasteStorageBackend *self)
{
    return g_paste_sqlite_backend_get_passphrase (self) ? G_PASTE_STORAGE_ENCRYPTED_SQLITE : G_PASTE_STORAGE_SQLITE;
}

static void
g_paste_sqlite_backend_finalize (GObject *object)
{
    GPasteSqliteBackend *self = G_PASTE_SQLITE_BACKEND (object);

    g_clear_pointer (&self->db, g_paste_sqlite_backend_close);
    g_clear_pointer (&self->db_path, g_free);
    g_mutex_clear (&self->lock);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_clear_pointer (&self->key, gcr_secure_memory_free);
    gcr_secure_memory_strfree (self->passphrase);
#endif

    G_OBJECT_CLASS (g_paste_sqlite_backend_parent_class)->finalize (object);
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* See the vfunc: only encrypted data this backend's key does not open refutes
 * the passphrase. No crypto parameters (a corrupt or foreign file) and an empty
 * history both say nothing -- the latter gets re-keyed on open instead of
 * locking the user out. */
static gboolean
g_paste_sqlite_backend_history_refutes_passphrase (GPasteStorageBackend *self,
                                                   const gchar          *name)
{
    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (self, name);
    sqlite3 *db = NULL;

    /* Read-only: verification must never create or touch anything. */
    if (sqlite3_open_v2 (path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
    {
        sqlite3_close (db);
        return FALSE;
    }

    guchar salt[crypto_pwhash_SALTBYTES];
    guint64 opslimit = 0;
    guint64 memlimit = 0;
    g_autofree guchar *check = NULL;
    gsize check_length = 0;

    if (!g_paste_sqlite_backend_load_crypto_params (db, salt, &opslimit, &memlimit, &check, &check_length))
    {
        sqlite3_close (db);
        return FALSE;
    }

    const gchar *passphrase = g_paste_sqlite_backend_get_passphrase (self);
    /* Where every other derived key lives: the stack is pageable, so a key on it
     * can reach the swap between here and the memzero below. */
    guchar *key = gcr_secure_memory_alloc (crypto_secretbox_KEYBYTES);
    gboolean derived = g_paste_sqlite_backend_derive_key (passphrase, salt, opslimit, memlimit, key);
    gboolean checks_out = derived && g_paste_sqlite_backend_key_checks_out (key, check, check_length);
    gboolean has_data = g_paste_sqlite_backend_query_int64 (db, "SELECT COUNT (*) FROM items;", 0) > 0;

    gcr_secure_memory_free (key);
    sqlite3_close (db);

    return derived && !checks_out && has_data;
}
#endif

static void
g_paste_sqlite_backend_class_init (GPasteSqliteBackendClass *klass)
{
    GPasteStorageBackendClass *storage_class = G_PASTE_STORAGE_BACKEND_CLASS (klass);

    storage_class->read_history_file = g_paste_sqlite_backend_read_history_file;
    storage_class->write_history_file = g_paste_sqlite_backend_write_history_file;
    storage_class->get_kind = g_paste_sqlite_backend_get_kind;
    storage_class->delete_history = g_paste_sqlite_backend_delete_history;
    storage_class->count_history = g_paste_sqlite_backend_count_history;

    storage_class->add_item = g_paste_sqlite_backend_add_item;
    storage_class->remove_item = g_paste_sqlite_backend_remove_item;
    storage_class->replace_item = g_paste_sqlite_backend_replace_item;
    storage_class->clear_history = g_paste_sqlite_backend_clear_history;

#ifdef G_PASTE_ENABLE_ENCRYPTION
    storage_class->rekey = g_paste_sqlite_backend_rekey;
    storage_class->history_refutes_passphrase = g_paste_sqlite_backend_history_refutes_passphrase;
#endif

    G_OBJECT_CLASS (klass)->finalize = g_paste_sqlite_backend_finalize;
}

static void
g_paste_sqlite_backend_init (GPasteSqliteBackend *self)
{
    g_mutex_init (&self->lock);
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
    GPasteSqliteBackend *backend = G_PASTE_SQLITE_BACKEND (self);

    backend->passphrase = gcr_secure_memory_strdup (passphrase);

    return self;
}

#endif
