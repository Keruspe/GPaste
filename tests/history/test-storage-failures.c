// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>
#include <gpaste-test-env.h>
#include <gpaste-daemon/gpaste-daemon-methods.h>
#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-file-backend.h>
#include <gpaste-daemon/gpaste-history.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-storage-migration.h>
#include <gpaste-daemon/gpaste-text-item.h>

#include <glib/gstdio.h>

#include <dlfcn.h>
#include <sys/file.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#ifdef G_PASTE_ENABLE_ENCRYPTION
#include <gpaste-daemon/gpaste-secret-stream-converter.h>

#include <sodium.h>

/* Every key this binary derives costs libsodium's minimum: the cases are about
 * files and transactions, not key strength, and the MODERATE parameters every
 * store is written with cost a third of a second and 256 MiB a derivation. The
 * parameters stored are still those, and every derivation here goes through
 * this one, a read's as much as a write's, so the two still agree. */
/* How many keys were derived, which is what a passphrase check costs. */
static gint derivations;

int
crypto_pwhash (unsigned char * const       out,
               unsigned long long          outlen,
               const char * const          passwd,
               unsigned long long          passwdlen,
               const unsigned char * const salt,
               unsigned long long          opslimit G_GNUC_UNUSED,
               size_t                      memlimit G_GNUC_UNUSED,
               int                         alg)
{
    typedef int (*Derive) (unsigned char *, unsigned long long, const char *, unsigned long long,
                           const unsigned char *, unsigned long long, size_t, int);
    Derive original = (Derive) dlsym (RTLD_NEXT, "crypto_pwhash");

    g_assert_nonnull (original);
    g_atomic_int_inc (&derivations);

    return original (out, outlen, passwd, passwdlen, salt, crypto_pwhash_OPSLIMIT_MIN, crypto_pwhash_MEMLIMIT_MIN, alg);
}

/* The size of every block gcr_secure_memory_alloc () handed out while
 * @secure_tracked is set, and how many of them went back holding anything:
 * gcr frees ordinary memory, which it falls back to, as it is. */
static gboolean secure_tracked;
static GHashTable *secure_sizes;
static guint secure_frees;
static guint unwiped_frees;
G_LOCK_DEFINE_STATIC (secure);

gpointer
gcr_secure_memory_alloc (gsize size)
{
    typedef gpointer (*Alloc) (gsize);
    Alloc original = (Alloc) dlsym (RTLD_NEXT, "gcr_secure_memory_alloc");
    gpointer memory = original (size);

    G_LOCK (secure);
    if (secure_tracked)
        g_hash_table_insert (secure_sizes, memory, GSIZE_TO_POINTER (size));
    G_UNLOCK (secure);

    return memory;
}

void
gcr_secure_memory_free (gpointer memory)
{
    typedef void (*Free) (gpointer);
    Free original = (Free) dlsym (RTLD_NEXT, "gcr_secure_memory_free");
    gpointer size;

    G_LOCK (secure);
    if (secure_sizes && g_hash_table_steal_extended (secure_sizes, memory, NULL, &size))
    {
        const guint8 *bytes = memory;

        ++secure_frees;
        for (gsize i = 0; i < GPOINTER_TO_SIZE (size); ++i)
        {
            if (bytes[i])
            {
                ++unwiped_frees;
                break;
            }
        }
    }
    G_UNLOCK (secure);

    original (memory);
}

#endif

#ifdef G_PASTE_ENABLE_ENCRYPTION
#include <malloc.h>

/* Found once, before anything here asks for it: dlsym () frees on its own,
 * which would come back into free () while it is being resolved. A block freed
 * before that -- the dynamic linker's own -- is left alone. glibc's
 * __libc_free () would spare the lookup, but musl does not export it. */
typedef void (*LibcFree) (void *);
typedef void *(*LibcRealloc) (void *, size_t);
static LibcFree original_free;
static LibcRealloc original_realloc;

__attribute__ ((constructor)) static void
resolve_free (void)
{
    original_free = (LibcFree) dlsym (RTLD_NEXT, "free");
    original_realloc = (LibcRealloc) dlsym (RTLD_NEXT, "realloc");
}

/* Every block freed while @freed_secret is set is searched for it, and counted
 * when it still holds it: how a copy of a decrypted password left behind in
 * freed memory shows. GMarkup's own buffers, freed with the parser's context,
 * are not GPaste's to wipe (see _g_paste_file_backend_read_or_count ()), and
 * go uncounted. */
static const gchar *freed_secret;
static guint freed_holding_secret;
static gboolean freeing_markup;

void
free (void *memory)
{
    const gchar *secret = freed_secret;

    if (memory && secret && !freeing_markup && memmem (memory, malloc_usable_size (memory), secret, strlen (secret)))
        ++freed_holding_secret;

    if (original_free)
        original_free (memory);
}

/* A block realloc () moves is released holding what it held, which free ()
 * never sees. Whether it moves is the heap's call, so while @freed_secret is
 * set every one does, and one holding half the secret or more -- what a growing
 * buffer had decoded of it so far -- is counted as a copy left behind too. */
void *
realloc (void   *memory,
         size_t  size)
{
    const gchar *secret = freed_secret;

    if (original_realloc && !(memory && size && secret && !freeing_markup))
        return original_realloc (memory, size);

    /* Moved by hand: there is no realloc () to call before resolve_free (). */
    void *moved = (size) ? malloc (size) : NULL;

    if (moved && memory)
        memcpy (moved, memory, MIN (size, malloc_usable_size (memory)));
    if (memory && secret && !freeing_markup && memmem (memory, malloc_usable_size (memory), secret, strlen (secret) / 2))
        ++freed_holding_secret;
    if (original_free)
        original_free (memory);

    return moved;
}

void
g_markup_parse_context_unref (GMarkupParseContext *context)
{
    typedef void (*Unref) (GMarkupParseContext *);
    Unref original = (Unref) dlsym (RTLD_NEXT, "g_markup_parse_context_unref");

    freeing_markup = TRUE;
    original (context);
    freeing_markup = FALSE;
}
#endif

/* The history loads handed to a thread, counted as they go: a load that has to
 * wait is told apart from one that has not run yet, however slow the worker. */
static guint loads_started;

void
g_task_run_in_thread (GTask          *task,
                      GTaskThreadFunc task_func)
{
    typedef void (*Run) (GTask *, GTaskThreadFunc);
    Run original = (Run) dlsym (RTLD_NEXT, "g_task_run_in_thread");

    g_assert_nonnull (original);
    if (g_paste_str_equal (g_task_get_name (task), "gpaste-history-load"))
        g_atomic_int_inc (&loads_started);
    original (task, task_func);
}

/* Moves whose source ends with one of these fail, as a filesystem giving out
 * would fail them: NULL for none. */
static const gchar * const *failed_moves;
/* The files fsync () fails for, by suffix, and every path it synced with its
 * size then (a gint64), which g_file_move () below checks what it installs
 * against: one that has grown since was synced before its last write. */
static const gchar * const *failed_syncs;
static GHashTable *synced;
static gboolean moved_unsynced;

/* The path @fd was opened on, through /proc. */
static gchar *
fd_path (gint fd)
{
    g_autofree gchar *link = g_strdup_printf ("/proc/self/fd/%d", fd);

    return g_file_read_link (link, NULL);
}

int
fsync (int fd)
{
    typedef int (*Sync) (int);
    Sync original = (Sync) dlsym (RTLD_NEXT, "fsync");
    g_autofree gchar *path = fd_path (fd);

    g_assert_nonnull (original);
    for (const gchar * const *suffix = failed_syncs; path && suffix && *suffix; ++suffix)
    {
        if (g_str_has_suffix (path, *suffix))
        {
            errno = EIO;

            return -1;
        }
    }

    GStatBuf st;

    if (!synced)
        synced = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    if (path && fstat (fd, &st) == 0)
    {
        gint64 size = st.st_size;

        g_hash_table_insert (synced, g_steal_pointer (&path), g_memdup2 (&size, sizeof (size)));
    }

    return original (fd);
}

/* What flock () fails with, as a home on NFS without lockd has it fail: 0 for
 * nothing. And, while @pause_taken_lock is set, the next flock () taking a
 * lock says so through @lock_taken and then holds on to the thread for a
 * moment, as one preempted between taking the lock and recording it would.
 * @busy_flocks counts the exclusive locks refused for being held, a wait for
 * the lock having started once there is one. */
static gint refused_flock;
static guint busy_flocks;
static gint pause_taken_lock;
static GMutex lock_taken_mutex;
static GCond lock_taken_cond;
static gboolean lock_taken;

int
flock (int fd,
       int operation)
{
    typedef int (*Flock) (int, int);
    Flock original = (Flock) dlsym (RTLD_NEXT, "flock");

    g_assert_nonnull (original);
    if (refused_flock)
    {
        errno = refused_flock;

        return -1;
    }

    gint taken = original (fd, operation);

    if (taken < 0 && errno == EWOULDBLOCK && (operation & LOCK_EX))
    {
        gint busy = errno;

        g_atomic_int_inc (&busy_flocks);
        errno = busy;
    }

    if (!taken && (operation & LOCK_EX) && g_atomic_int_compare_and_exchange (&pause_taken_lock, TRUE, FALSE))
    {
        g_mutex_lock (&lock_taken_mutex);
        lock_taken = TRUE;
        g_cond_broadcast (&lock_taken_cond);
        g_mutex_unlock (&lock_taken_mutex);
        g_usleep (300 * 1000);
    }

    return taken;
}

/* Whether link () fails as a filesystem without hard links has it fail. */
static gboolean refused_links;

int
link (const char *from,
      const char *to)
{
    typedef int (*Link) (const char *, const char *);
    Link original = (Link) dlsym (RTLD_NEXT, "link");

    g_assert_nonnull (original);
    if (refused_links)
    {
        errno = EPERM;

        return -1;
    }

    return original (from, to);
}

gboolean
g_file_move (GFile                *source,
             GFile                *destination,
             GFileCopyFlags        flags,
             GCancellable         *cancellable,
             GFileProgressCallback progress,
             gpointer              data,
             GError              **error)
{
    typedef gboolean (*Move) (GFile *, GFile *, GFileCopyFlags, GCancellable *, GFileProgressCallback, gpointer, GError **);
    Move original = (Move) dlsym (RTLD_NEXT, "g_file_move");
    g_autofree gchar *path = g_file_get_path (source);

    g_assert_nonnull (original);
    for (const gchar * const *suffix = failed_moves; suffix && *suffix; ++suffix)
    {
        if (g_str_has_suffix (path, *suffix))
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "injected failure moving %s", path);

            return FALSE;
        }
    }

    /* Resolved, as fd_path () reads it back: the test directories may be
     * reached through a symlink, a TMPDIR or a home that is one. */
    g_autofree gchar *real = realpath (path, NULL);
    const gint64 *synced_size = (synced && real) ? g_hash_table_lookup (synced, real) : NULL;
    GStatBuf st;

    if ((g_str_has_suffix (path, ".tmp") || g_str_has_suffix (path, ".rekey")) &&
        !(synced_size && g_stat (real, &st) == 0 && st.st_size == *synced_size))
        moved_unsynced = TRUE;

    return original (source, destination, flags, cancellable, progress, data, error);
}

#ifdef G_PASTE_ENABLE_SQLITE
#include <gpaste-daemon/gpaste-sqlite-backend.h>

#include <sqlite3.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC (sqlite3, sqlite3_close)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (sqlite3_stmt, sqlite3_finalize)

/* The statement whose execution fails as a full disk would fail it, matched
 * as a substring -- the next one only, with @failed_sql_once -- and the
 * database another GPaste upgrades the moment a transaction is about to begin:
 * NULL for none. */
static const gchar *failed_sql;
static gboolean failed_sql_once;
static const gchar *upgraded_meanwhile;
/* How many full writes began emptying their table, and how many statements
 * @failed_sql failed. */
static guint full_writes;
static guint injected_failures;

int
sqlite3_exec (sqlite3    *db,
              const char *sql,
              int       (*callback) (void *, int, char **, char **),
              void       *data,
              char      **error)
{
    typedef int (*Exec) (sqlite3 *, const char *, int (*) (void *, int, char **, char **), void *, char **);
    Exec original = (Exec) dlsym (RTLD_NEXT, "sqlite3_exec");

    g_assert_nonnull (original);
    if (!strcmp (sql, "DELETE FROM items;"))
        ++full_writes;

    if (failed_sql && strstr (sql, failed_sql))
    {
        if (failed_sql_once)
            failed_sql = NULL;
        ++injected_failures;
        if (error)
            *error = sqlite3_mprintf ("injected disk-full failure");

        return SQLITE_FULL;
    }

    if (upgraded_meanwhile && strstr (sql, "BEGIN IMMEDIATE"))
    {
        g_autoptr (sqlite3) other = NULL;

        g_assert_cmpint (sqlite3_open (upgraded_meanwhile, &other), ==, SQLITE_OK);
        g_assert_cmpint (original (other,
                                   "ALTER TABLE items ADD COLUMN favourite INTEGER NOT NULL DEFAULT 0;"
                                   "ALTER TABLE items ADD COLUMN timeout INTEGER NOT NULL DEFAULT 0;"
                                   "PRAGMA user_version = 3;",
                                   NULL, NULL, NULL), ==, SQLITE_OK);
        upgraded_meanwhile = NULL;
    }

    return original (db, sql, callback, data, error);
}

/* The statement whose next step fails as a busy database would fail it, once
 * @busy_step_skip of its steps have gone through: NULL for none. */
static const gchar *busy_step_sql;
static guint busy_step_skip;

int
sqlite3_step (sqlite3_stmt *statement)
{
    typedef int (*Step) (sqlite3_stmt *);
    Step original = (Step) dlsym (RTLD_NEXT, "sqlite3_step");

    g_assert_nonnull (original);
    if (busy_step_sql && !g_strcmp0 (sqlite3_sql (statement), busy_step_sql))
    {
        if (busy_step_skip)
            --busy_step_skip;
        else
        {
            busy_step_sql = NULL;

            return SQLITE_BUSY;
        }
    }

    return original (statement);
}

/* How many checkpoints were asked of SQLite. */
static guint checkpoints;

int
sqlite3_wal_checkpoint_v2 (sqlite3    *db,
                           const char *database,
                           int         mode,
                           int        *log_frames,
                           int        *checkpointed_frames)
{
    typedef int (*Checkpoint) (sqlite3 *, const char *, int, int *, int *);
    Checkpoint original = (Checkpoint) dlsym (RTLD_NEXT, "sqlite3_wal_checkpoint_v2");

    g_assert_nonnull (original);
    ++checkpoints;

    return original (db, database, mode, log_frames, checkpointed_frames);
}

/* The statement whose next preparation fails, as an allocation failing would
 * fail it: NULL for none. */
static const gchar *failed_prepare_sql;

int
sqlite3_prepare_v2 (sqlite3       *db,
                    const char    *sql,
                    int            length,
                    sqlite3_stmt **statement,
                    const char   **tail)
{
    typedef int (*Prepare) (sqlite3 *, const char *, int, sqlite3_stmt **, const char **);
    Prepare original = (Prepare) dlsym (RTLD_NEXT, "sqlite3_prepare_v2");

    g_assert_nonnull (original);
    if (failed_prepare_sql && !g_strcmp0 (sql, failed_prepare_sql))
    {
        failed_prepare_sql = NULL;
        *statement = NULL;

        return SQLITE_NOMEM;
    }

    return original (db, sql, length, statement, tail);
}

/* The binds of a value longer than this many bytes fail with @refused_bind,
 * leaving their parameter NULL as SQLite does: 0 for none. SQLITE_TOOBIG is
 * what SQLite answers past SQLITE_MAX_LENGTH, which no test can afford to
 * reach. */
static gsize refused_bind_length;
static gint refused_bind = SQLITE_TOOBIG;

int
sqlite3_bind_blob64 (sqlite3_stmt  *statement,
                     int            position,
                     const void    *data,
                     sqlite3_uint64 length,
                     void         (*destructor) (void *))
{
    typedef int (*Bind) (sqlite3_stmt *, int, const void *, sqlite3_uint64, void (*) (void *));
    Bind original = (Bind) dlsym (RTLD_NEXT, "sqlite3_bind_blob64");

    g_assert_nonnull (original);
    if (refused_bind_length && length > refused_bind_length)
        return refused_bind;

    return original (statement, position, data, length, destructor);
}

int
sqlite3_bind_text (sqlite3_stmt *statement,
                   int           position,
                   const char   *text,
                   int           length,
                   void        (*destructor) (void *))
{
    typedef int (*Bind) (sqlite3_stmt *, int, const char *, int, void (*) (void *));
    Bind original = (Bind) dlsym (RTLD_NEXT, "sqlite3_bind_text");

    g_assert_nonnull (original);
    if (refused_bind_length && ((length < 0) ? strlen (text) : (gsize) length) > refused_bind_length)
        return refused_bind;

    return original (statement, position, text, length, destructor);
}

/* One integer out of @path, through a connection of its own: test-history.c's
 * sqlite_raw_count() is the same, in a binary that shares no fixture with
 * this one. */
static gint64
sqlite_query_int (const gchar *path,
                  const gchar *sql)
{
    g_autoptr (sqlite3) db = NULL;
    g_autoptr (sqlite3_stmt) statement = NULL;

    g_assert_cmpint (sqlite3_open_v2 (path, &db, SQLITE_OPEN_READONLY, NULL), ==, SQLITE_OK);
    g_assert_cmpint (sqlite3_prepare_v2 (db, sql, -1, &statement, NULL), ==, SQLITE_OK);
    g_assert_cmpint (sqlite3_step (statement), ==, SQLITE_ROW);

    return sqlite3_column_int64 (statement, 0);
}
#endif

/* What the journal held each time a history's re-key began, while this is
 * set: what a process dying between two would leave. From the re-key counted
 * @failed_rekey on, the history's install fails. */
static GPtrArray *rekey_journals;
static guint failed_rekey;

gboolean
g_paste_storage_backend_rekey (GPasteStorageBackend *self,
                               const gchar          *name,
                               const gchar          *new_passphrase,
                               gboolean             *split)
{
    typedef gboolean (*Rekey) (GPasteStorageBackend *, const gchar *, const gchar *, gboolean *);
    Rekey original = (Rekey) dlsym (RTLD_NEXT, "g_paste_storage_backend_rekey");

    g_assert_nonnull (original);
    if (rekey_journals)
    {
        g_autofree gchar *dir = g_paste_util_get_history_dir_path ();
        g_autofree gchar *journal = g_build_filename (dir, "storage-journal", NULL);
        gchar *contents = NULL;

        g_file_get_contents (journal, &contents, NULL, NULL);
        g_ptr_array_add (rekey_journals, contents ? contents : g_strdup (""));

        static const gchar * const history_install[] = { ".xmls.rekey", NULL };

        if (failed_rekey && rekey_journals->len >= failed_rekey)
            failed_moves = history_install;
    }

    return original (self, name, new_passphrase, split);
}

/* Writes through a backend of the first kind leave the history's first item
 * out, as a destination that took a write and does not give it back would;
 * file writes through one of the second kind lose the document's footer, as an
 * interrupted one would. -1 for none. */
static gint shortened_kind = -1;
/* Whether the journal was on the disk when a history was written through a
 * backend of this kind, a write of which is what it has to be there for. */
static gint journaled_kind = -1;
static gboolean journal_at_write;
static gint truncated_kind = -1;

gboolean
g_paste_storage_backend_write_history (GPasteStorageBackend *backend,
                                       const gchar          *name,
                                       const GList          *items)
{
    typedef gboolean (*Write) (GPasteStorageBackend *, const gchar *, const GList *);
    Write original = (Write) dlsym (RTLD_NEXT, "g_paste_storage_backend_write_history");
    gint kind = g_paste_storage_backend_get_kind (backend);

    g_assert_nonnull (original);
    if (items && kind == shortened_kind)
        items = items->next;
    if (kind == journaled_kind)
    {
        /* journal_path ()'s, the migration being included further down. */
        g_autofree gchar *dir = g_paste_util_get_history_dir_path ();
        g_autofree gchar *journal = g_build_filename (dir, "storage-journal", NULL);

        journal_at_write = g_file_test (journal, G_FILE_TEST_EXISTS);
    }

    gboolean written = original (backend, name, items);

    if (kind != truncated_kind)
        return written;

    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (backend, name);
    g_autofree gchar *contents = NULL;

    g_assert_true (g_file_get_contents (path, &contents, NULL, NULL));

    const gchar *footer = strstr (contents, "</history>");

    g_assert_nonnull (footer);
    g_assert_true (g_file_set_contents (path, contents, footer - contents, NULL));

    return written;
}

/* The offered backends less this kind, standing for a build without the
 * feature it needs: -1 for none. The library asks through its own exported
 * symbol, which this one takes the place of. */
static gint hidden_kind = -1;
static guint listed;

const GPasteStorage *
g_paste_prompt_list_storage_backends (guint *n_offered)
{
    typedef const GPasteStorage *(*List) (guint *);
    List original = (List) dlsym (RTLD_NEXT, "g_paste_prompt_list_storage_backends");
    static GPasteStorage kept[G_PASTE_N_STORAGE];

    g_assert_nonnull (original);
    ++listed;

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

#if defined(G_PASTE_ENABLE_ENCRYPTION) || defined(G_PASTE_ENABLE_SQLITE)
/* Whether hiding a kind reaches the library's own calls, which go through the
 * interposer above only while they go through the PLT: -Bsymbolic binds them
 * inside the library, and a case hiding a kind skips itself then. Every other
 * case interposes calls the library makes into another one, or that the
 * migration makes from its copy built into this binary, which -Bsymbolic
 * leaves alone. The cases hiding one are those of a flavour that can be
 * missing. */
static gboolean
kind_hidable (void)
{
    guint before = listed;

    g_paste_prompt_storage_is_offered (G_PASTE_STORAGE_FILE);
    if (listed != before)
        return TRUE;

    g_test_skip ("libgpaste-daemon binds its own calls (-Bsymbolic)");

    return FALSE;
}
#endif

/* A folder of the history directory swapped for a link to @swap_target right
 * after the store check has looked at it, when set: whichever look it takes,
 * by path or by its parent's descriptor. */
static const gchar *swap_dir;
static const gchar *swap_target;

static void
swap_looked_at (const gchar *looked)
{
    if (!swap_dir || (!g_str_has_suffix (looked, "/swapped-for-link") && !g_paste_str_equal (looked, "swapped-for-link")))
        return;

    g_autofree gchar *entry = g_build_filename (swap_dir, "swapped-for-link", NULL);

    swap_dir = NULL;
    g_assert_cmpint (g_rmdir (entry), ==, 0);
    g_assert_cmpint (symlink (swap_target, entry), ==, 0);
}

int
lstat (const char  *path,
       struct stat *buf)
{
    typedef int (*Lstat) (const char *, struct stat *);
    Lstat original = (Lstat) dlsym (RTLD_NEXT, "lstat");
    int result = original (path, buf);

    swap_looked_at (path);
    return result;
}

int
fstatat (int          dirfd,
         const char  *path,
         struct stat *buf,
         int          flags)
{
    typedef int (*Fstatat) (int, const char *, struct stat *, int);
    Fstatat original = (Fstatat) dlsym (RTLD_NEXT, "fstatat");
    int result = original (dirfd, path, buf, flags);

    swap_looked_at (path);
    return result;
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
    const gchar  *passphrase;
    guint         reports;
    gchar        *title;
    gchar        *message;
    /* Each passphrase asked for, in turn, where @passphrase answers them all,
     * and what the last request said was wrong with the one before. */
    const gchar * const *passphrases;
    guint                passphrase_asks;
    gchar               *passphrase_error;
    /* Whether the last passphrase was asked for twice. */
    gboolean             confirmed;
    /* What the last request explained, and whether it offered to remember. */
    gchar               *passphrase_description;
    gboolean             offered_remember;
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
test_migration_prompt_passphrase (GPastePrompt        *prompt,
                                  GPastePromptRequest *request)
{
    TestMigrationPrompt *self = TEST_MIGRATION_PROMPT (prompt);
    const gchar *passphrase = self->passphrase;

    /* A list ends with NULL, and asked once more than it holds answers that: a
     * dismissal, rather than whatever follows the list. */
    if (self->passphrases)
    {
        g_assert_cmpuint (self->passphrase_asks, <=, g_strv_length ((GStrv) self->passphrases));
        passphrase = self->passphrases[self->passphrase_asks];
    }

    ++self->passphrase_asks;
    self->confirmed = g_paste_prompt_request_get_confirm (request);
    g_set_str (&self->passphrase_error, g_paste_prompt_request_get_error_message (request));
    g_set_str (&self->passphrase_description, g_paste_prompt_request_get_description (request));
    self->offered_remember = g_paste_prompt_request_offers_remember (request);
    g_paste_prompt_request_reply_passphrase (request, passphrase, G_PASTE_STORAGE_REMEMBER_UNCHANGED);
}

static void
test_migration_prompt_report (GPastePrompt *prompt,
                              const gchar  *title,
                              const gchar  *message)
{
    TestMigrationPrompt *self = TEST_MIGRATION_PROMPT (prompt);

    ++self->reports;
    g_set_str (&self->title, title);
    g_set_str (&self->message, message);
}

#if defined(G_PASTE_ENABLE_ENCRYPTION) || defined(G_PASTE_ENABLE_SQLITE)
/* The one report a failed import makes: its own title, and the reason after
 * the standing description. */
static void
assert_import_failure_reported (TestMigrationPrompt *prompt)
{
    g_autofree gchar *standing = g_strconcat (g_paste_prompt_text (G_PASTE_PROMPT_TEXT_IMPORT_FAILED_DESCRIPTION), "\n\n", NULL);

    g_assert_cmpuint (prompt->reports, ==, 1);
    g_assert_cmpstr (prompt->title, ==, g_paste_prompt_text (G_PASTE_PROMPT_TEXT_IMPORT_FAILED_TITLE));
    g_assert_true (g_str_has_prefix (prompt->message, standing));
    g_assert_cmpuint (strlen (prompt->message), >, strlen (standing));
}
#endif

static void
test_migration_prompt_iface_init (GPastePromptInterface *iface)
{
    iface->passphrase = test_migration_prompt_passphrase;
    iface->migration = test_migration_prompt_migration;
    iface->report = test_migration_prompt_report;
}

static void
test_migration_prompt_finalize (GObject *object)
{
    g_free (TEST_MIGRATION_PROMPT (object)->title);
    g_free (TEST_MIGRATION_PROMPT (object)->message);
    g_free (TEST_MIGRATION_PROMPT (object)->passphrase_error);
    g_free (TEST_MIGRATION_PROMPT (object)->passphrase_description);

    G_OBJECT_CLASS (test_migration_prompt_parent_class)->finalize (object);
}

static void
test_migration_prompt_class_init (TestMigrationPromptClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = test_migration_prompt_finalize;
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

#if defined(G_PASTE_ENABLE_SQLITE) || defined(G_PASTE_ENABLE_ENCRYPTION)
/* A storage worker running, or the concern answering without one: either ends
 * the wait for a worker to be seen, rather than meson's timeout. */
static gboolean
busy_or_answered (gconstpointer result,
                  gconstpointer arg G_GNUC_UNUSED)
{
    return g_paste_storage_is_busy () || *(gpointer const *) result;
}
#endif

#ifdef G_PASTE_ENABLE_SQLITE
/* A migration's import rewrites the store on a thread of its own, which a host
 * waits out before it ends or lets go of the storage lock. */
static void
test_import_busy (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);
    g_paste_settings_set_storage_backend_revision (settings, G_PASTE_STORAGE_BACKEND_REVISION - 1);

    {
        g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("imported"));

        g_paste_storage_backend_write_history (file, name, items);
    }

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    prompt->chosen = G_PASTE_STORAGE_SQLITE;
    prompt->import = TRUE;
    g_paste_storage_migration_async (G_PASTE_PROMPT (prompt), settings, on_migration_settled, &result);
    /* As in /storage/rekey/busy. */
    g_paste_test_bus_wait_until (busy_or_answered, &result, NULL);
    g_assert_true (g_paste_storage_is_busy ());
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &result, NULL);
    g_assert_false (g_paste_storage_is_busy ());
    g_assert_true (g_paste_storage_migration_finish (result, &error));
    g_assert_no_error (error);
}
#endif

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
        if (!kind_hidable ())
            return;

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

/* A failed import out of the encrypted file flavour into the plain one takes its
 * copy back with the screenshots it decrypted on the way: they are the very
 * pixels the source keeps encrypted. */
static void
test_import_failure_leaves_no_cleartext (void)
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
    const guchar pixel[] = { 53, 63, 73, 255 };
    g_autoptr (GBytes) bytes = g_bytes_new (pixel, sizeof (pixel));
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (1, 1, GDK_MEMORY_R8G8B8A8, bytes, 4);
    GPasteItem *image = g_paste_image_item_new (texture);
    g_autofree gchar *png = g_paste_file_backend_image_path (name, g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (image)));
    g_autofree gchar *pngs = g_paste_file_backend_encrypted_path (png);

    g_paste_settings_set_images_support (settings, TRUE);
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_paste_settings_set_storage_backend_revision (settings, G_PASTE_STORAGE_BACKEND_REVISION - 1);

    {
        g_autoptr (GPasteStorageBackend) encrypted = g_paste_file_backend_new_encrypted (settings, "passphrase");
        /* The text first, which the injected failure leaves out, so that the
         * image is written before the read-back finds the copy short. */
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("text"));

        items = g_list_append (items, image);
        g_paste_storage_backend_write_history (encrypted, name, items);
    }

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GError) error = NULL;

    prompt->chosen = G_PASTE_STORAGE_FILE;
    prompt->import = TRUE;
    prompt->cleanup = TRUE;
    g_paste_storage_backend_set_passphrase ("passphrase");
    shortened_kind = G_PASTE_STORAGE_FILE;
    g_assert_true (run_migration (settings, prompt, &error));
    g_assert_no_error (error);
    shortened_kind = -1;
    g_paste_storage_backend_set_passphrase (NULL);

    assert_import_failure_reported (prompt);
    g_assert_false (g_file_test (png, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (pngs, G_FILE_TEST_EXISTS));
}

/* The passphrase for a destination already holding histories -- kept by an
 * earlier migration away from it -- is the one they take: another would have
 * the import write the history under one and leave them under another. */
static void
test_migration_destination_passphrase (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    const gchar * const passphrases[] = { "new", "old", NULL };

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);
    g_paste_settings_set_storage_backend_revision (settings, G_PASTE_STORAGE_BACKEND_REVISION - 1);

    {
        g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_autoptr (GPasteStorageBackend) kept = g_paste_file_backend_new_encrypted (settings, "old");
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("history"));
        g_autolist (GPasteItem) work = g_list_append (NULL, g_paste_text_item_new ("work"));

        g_paste_storage_backend_write_history (file, name, items);
        g_paste_storage_backend_write_history (kept, "work", work);
    }

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GError) error = NULL;

    prompt->chosen = G_PASTE_STORAGE_ENCRYPTED_FILE;
    prompt->import = TRUE;
    prompt->passphrases = passphrases;
    g_assert_true (run_migration (settings, prompt, &error));
    g_assert_no_error (error);

    g_assert_cmpuint (prompt->passphrase_asks, ==, 2);
    g_assert_nonnull (prompt->passphrase_error);
    g_assert_cmpint (g_paste_settings_get_storage_backend (settings), ==, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_assert_true (g_paste_storage_passphrase_can_decrypt (G_PASTE_STORAGE_ENCRYPTED_FILE, settings, "old"));
    g_paste_storage_backend_set_passphrase (NULL);
}

/* A read of an encrypted history leaves no copy of a password it holds in freed
 * memory: the decrypted document, the text the parser hands over and the value
 * decoded from it are all wiped before they go. */
static void
test_encrypted_read_leaves_no_copy (void)
{
    const gchar *name = "no-copy-left";
    const gchar *secret = "a-password-nobody-should-find-in-freed-memory";
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "passphrase");
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_password_item_new ("name", secret, 0));

    items = g_list_append (items, g_paste_text_item_new ("a text"));
    g_assert_true (g_paste_storage_backend_write_history (backend, name, items));

    g_autolist (GPasteItem) loaded = NULL;
    gsize size;

    freed_holding_secret = 0;
    freed_secret = secret;
    g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &loaded, &size));
    freed_secret = NULL;
    g_assert_cmpuint (g_list_length (loaded), ==, 2);
    g_assert_cmpuint (freed_holding_secret, ==, 0);
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
    if (!kind_hidable ())
        return;

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
    if (!kind_hidable ())
        return;

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

    assert_import_failure_reported (prompt);
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

/* An import copies every item a history stores, as the cleanup that follows
 * deletes them all: read as a load does, a history past max-history-size --
 * one not loaded since the cap was lowered -- would lose what is past it, and
 * every history its screenshots while images are turned off. */
static void
test_import_copies_whole (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "past-the-cap";
    const guchar pixel[] = { 12, 34, 56, 255 };
    g_autoptr (GBytes) bytes = g_bytes_new (pixel, sizeof (pixel));
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (1, 1, GDK_MEMORY_R8G8B8A8, bytes, 4);

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);
    g_paste_settings_set_images_support (settings, TRUE);

    {
        g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_image_item_new (texture));

        for (guint i = 0; i < 7; ++i)
        {
            g_autofree gchar *text = g_strdup_printf ("text %u", i);

            items = g_list_append (items, g_paste_text_item_new (text));
        }
        g_paste_storage_backend_write_history (file, name, items);
    }

    g_paste_settings_set_max_history_size (settings, 5);
    g_paste_settings_set_images_support (settings, FALSE);

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GError) error = NULL;

    prompt->chosen = G_PASTE_STORAGE_SQLITE;
    prompt->import = TRUE;
    prompt->cleanup = TRUE;
    journaled_kind = G_PASTE_STORAGE_SQLITE;
    g_assert_true (run_migration (settings, prompt, &error));
    g_assert_no_error (error);
    journaled_kind = -1;
    g_assert_true (journal_at_write);
    g_assert_cmpint (g_paste_settings_get_storage_backend (settings), ==, G_PASTE_STORAGE_SQLITE);

    g_autofree gchar *copy = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_SQLITE));

    g_assert_cmpint (sqlite_query_int (copy, "SELECT COUNT (*) FROM items;"), ==, 8);
    g_assert_cmpint (sqlite_query_int (copy, "SELECT COUNT (*) FROM items WHERE image IS NOT NULL;"), ==, 1);

    g_autofree gchar *journal = journal_path ();

    g_assert_false (g_file_test (journal, G_FILE_TEST_EXISTS));
    g_paste_settings_reset (settings, G_PASTE_MAX_HISTORY_SIZE_SETTING);
    g_paste_settings_reset (settings, G_PASTE_IMAGES_SUPPORT_SETTING);
}

/* What an import a process did not live to finish wrote is taken back at the
 * next start -- in clear, for one out of an encrypted history -- unless the
 * move had already happened, which makes it the store. The history it was
 * copied from stays as it is either way. */
static void
test_import_cut_short (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "cut-short";
    g_autofree gchar *source = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_FILE));
    g_autofree gchar *copy = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_SQLITE));
    g_autofree gchar *journal = journal_path ();
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("kept"));
    g_autoptr (GPtrArray) journaled = g_ptr_array_new ();

    g_ptr_array_add (journaled, (gpointer) name);
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);

    {
        g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);

        g_paste_storage_backend_write_history (file, name, items);
    }

    for (guint moved = 0; moved < 2; ++moved)
    {
        {
            g_autoptr (GPasteStorageBackend) database = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);

            g_paste_storage_backend_write_history (database, name, items);
        }

        g_assert_true (journal_write (JOURNAL_IMPORT, G_PASTE_STORAGE_SQLITE, journaled, NULL));
        if (moved)
            g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_SQLITE);

        g_assert_false (g_paste_storage_decryption_needed (settings));
        g_assert_cmpint (g_file_test (copy, G_FILE_TEST_EXISTS), ==, moved);
        g_assert_true (g_file_test (source, G_FILE_TEST_EXISTS));
        g_assert_false (g_file_test (journal, G_FILE_TEST_EXISTS));
    }

    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
}

/* And before a migration asks anything, the standalone daemon asking for one
 * first: a copy cut short that does not read back would stop the next import
 * into the same flavour. */
static void
test_import_cut_short_then_migrate (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    g_autofree gchar *copy = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_SQLITE));
    g_autoptr (GPtrArray) journaled = g_ptr_array_new ();

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);
    g_paste_settings_set_storage_backend_revision (settings, G_PASTE_STORAGE_BACKEND_REVISION - 1);

    {
        g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("kept"));

        g_paste_storage_backend_write_history (file, name, items);
    }

    g_assert_true (g_file_set_contents (copy, "half a database", -1, NULL));
    g_ptr_array_add (journaled, (gpointer) name);
    g_assert_true (journal_write (JOURNAL_IMPORT, G_PASTE_STORAGE_SQLITE, journaled, NULL));

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GError) error = NULL;

    prompt->chosen = G_PASTE_STORAGE_SQLITE;
    prompt->import = TRUE;
    g_assert_true (run_migration (settings, prompt, &error));
    g_assert_no_error (error);
    g_assert_cmpint (g_paste_settings_get_storage_backend (settings), ==, G_PASTE_STORAGE_SQLITE);
    g_assert_cmpint (sqlite_query_int (copy, "SELECT COUNT (*) FROM items;"), ==, 1);
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

    assert_import_failure_reported (prompt);
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

static void
on_emptied (GObject      *source G_GNUC_UNUSED,
            GAsyncResult *result,
            gpointer      user_data)
{
    g_set_object ((GAsyncResult **) user_data, result);
}

/* EmptyHistory, waited for as its D-Bus answer is. */
static void
empty_history (const GPasteDaemonMethods *methods,
               const gchar               *name,
               GError                   **error)
{
    g_autoptr (GAsyncResult) result = NULL;

    g_paste_daemon_methods_empty_history_async (methods, name, on_emptied, &result);
    while (!result)
        g_main_context_iteration (NULL, TRUE);
    g_paste_daemon_methods_empty_history_finish (result, error);
}

#ifdef G_PASTE_ENABLE_SQLITE
/* An add evicting the oldest item, which every copy does once the history is
 * full, leaves the write-ahead log to SQLite's own checkpoints: what a delete
 * takes is checkpointed out of it at once, but an item aged out is nothing the
 * user deleted, and a checkpoint on every copy syncs the disk on every copy. */
static void
test_sqlite_eviction_checkpoints_nothing (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    const gchar *name = "eviction-checkpoints";
    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (backend, name);
    g_autoptr (GPasteItem) a = g_paste_text_item_new ("a");
    g_autoptr (GPasteItem) b = g_paste_text_item_new ("b");
    g_autoptr (GPasteItem) c = g_paste_text_item_new ("c");
    g_autoptr (GList) history = g_list_prepend (g_list_prepend (NULL, a), b);

    g_paste_storage_backend_write_history (backend, name, history);

    /* a evicted by c. */
    g_autoptr (GList) evicted = g_list_prepend (g_list_prepend (NULL, b), c);

    checkpoints = 0;
    g_paste_storage_backend_add_item (backend, name, c, evicted);
    g_assert_cmpuint (checkpoints, ==, 0);
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items;"), ==, 2);

    evicted = g_list_remove (evicted, b);
    g_paste_storage_backend_remove_item (backend, name, g_paste_item_get_uuid (b), evicted);
    g_assert_cmpuint (checkpoints, ==, 1);
}

/* A version 1 database as an older GPaste left it, at @path, holding one item
 * whose value is "precious". */
static void
write_version_1 (const gchar *path)
{
    g_autoptr (sqlite3) db = NULL;

    g_assert_true (g_paste_util_ensure_history_dir_exists ());
    g_assert_cmpint (sqlite3_open (path, &db), ==, SQLITE_OK);
    g_assert_cmpint (sqlite3_exec (db,
                                   "CREATE TABLE items (id INTEGER PRIMARY KEY, uuid TEXT NOT NULL UNIQUE, kind TEXT NOT NULL,"
                                   "                    value TEXT NOT NULL, rank INTEGER NOT NULL, date INTEGER, checksum TEXT,"
                                   "                    name TEXT, image BLOB);"
                                   "CREATE UNIQUE INDEX items_rank ON items (rank DESC);"
                                   "CREATE TABLE special_values (item_id INTEGER NOT NULL REFERENCES items (id) ON DELETE CASCADE,"
                                   "                             position INTEGER NOT NULL, mime TEXT NOT NULL, data BLOB NOT NULL,"
                                   "                             PRIMARY KEY (item_id, position));"
                                   "INSERT INTO items (uuid, kind, value, rank) VALUES ('952f0c74-bf9e-4d4f-b493-dd7d30998150', 'Text', 'precious', 1);"
                                   "PRAGMA user_version = 1;",
                                   NULL, NULL, NULL), ==, SQLITE_OK);
}

/* Read @name through a fresh SQLite backend: whether it read, and its one item
 * being the one write_version_1() stored when it did. */
static gboolean
read_version_1 (GPasteSettings *settings,
                const gchar    *name)
{
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autolist (GPasteItem) items = NULL;
    gsize size;

    if (!g_paste_storage_backend_read_history (backend, name, NULL, &items, &size))
        return FALSE;

    g_assert_cmpuint (g_list_length (items), ==, 1);
    g_assert_cmpstr (g_paste_item_get_value (items->data), ==, "precious");

    return TRUE;
}

/* A checksum names an image's file and is written back as it is, from any
 * flavour into any other: one a database row holds that is not a checksum is
 * computed again, as the file flavour's read does with its attribute. */
static void
test_sqlite_checksum_refused (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "checksum-refused";
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_SQLITE));
    const guchar pixel[] = { 83, 93, 103, 255 };
    g_autoptr (GBytes) bytes = g_bytes_new (pixel, sizeof (pixel));
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (1, 1, GDK_MEMORY_R8G8B8A8, bytes, 4);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_image_item_new (texture));
    const gchar *checksum = g_paste_image_item_get_checksum (items->data);

    g_paste_settings_set_images_support (settings, TRUE);

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
        g_autoptr (sqlite3) db = NULL;

        g_paste_storage_backend_write_history (backend, name, items);
        g_assert_cmpint (sqlite3_open (path, &db), ==, SQLITE_OK);
        g_assert_cmpint (sqlite3_exec (db, "UPDATE items SET checksum = '../../escaped\" kind=\"Password';", NULL, NULL, NULL), ==, SQLITE_OK);
    }

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autolist (GPasteItem) loaded = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &loaded, &size));
    g_assert_cmpuint (g_list_length (loaded), ==, 1);
    g_assert_cmpstr (g_paste_image_item_get_checksum (loaded->data), ==, checksum);
}

/* An incremental write that fails -- a full disk -- is not lost for good:
 * nothing retries it, and the next add's reconciliation only drops rows, so
 * that add, which carries the whole snapshot, writes it in full instead. */
static void
test_sqlite_diverged_write (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*injected disk-full failure*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    const gchar *name = "diverged-write";
    g_autoptr (GPasteItem) a = g_paste_text_item_new ("a");
    g_autoptr (GPasteItem) b = g_paste_text_item_new ("b");
    g_autoptr (GPasteItem) c = g_paste_text_item_new ("c");
    g_autoptr (GList) history = g_list_prepend (NULL, a);

    g_paste_storage_backend_write_history (backend, name, history);

    history = g_list_prepend (history, b);
    failed_sql = "BEGIN IMMEDIATE";
    g_paste_storage_backend_add_item (backend, name, b, history);
    failed_sql = NULL;

    history = g_list_prepend (history, c);
    g_paste_storage_backend_add_item (backend, name, c, history);

    g_autolist (GPasteItem) items = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &items, &size));
    g_assert_cmpuint (g_list_length (items), ==, 3);
    g_assert_cmpstr (g_paste_item_get_value (items->next->data), ==, "b");
}

/* A full write failing on a database it created leaves none behind, as the file
 * flavour leaves no document: a backup answered for would otherwise be listed
 * while holding nothing -- whether the write or the open making the schema
 * fails. One that was there before the write stays as it was, until the next
 * add carrying the history writes it in full: the reconciliation would only
 * drop rows, leaving out what the failed write had. */
static void
test_sqlite_failed_write_leaves_no_database (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*injected disk-full failure*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    const gchar *name = "failed-backup";
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_SQLITE));
    g_autolist (GPasteItem) first = g_list_append (NULL, g_paste_text_item_new ("first"));
    g_autolist (GPasteItem) second = g_list_append (NULL, g_paste_text_item_new ("second"));

    failed_sql = "DELETE FROM items;";
    g_paste_storage_backend_write_history (backend, name, first);
    failed_sql = NULL;
    g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));

    failed_sql = "CREATE TABLE IF NOT EXISTS items";
    g_paste_storage_backend_write_history (backend, name, first);
    failed_sql = NULL;
    g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));

    g_paste_storage_backend_write_history (backend, name, first);
    failed_sql = "DELETE FROM items;";
    g_paste_storage_backend_write_history (backend, name, second);
    failed_sql = NULL;

    {
        g_autolist (GPasteItem) items = NULL;
        gsize size;

        g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &items, &size));
        g_assert_cmpuint (g_list_length (items), ==, 1);
        g_assert_cmpstr (g_paste_item_get_value (items->data), ==, "first");
    }

    g_autoptr (GPasteItem) third = g_paste_text_item_new ("third");
    g_autoptr (GList) history = g_list_prepend (g_list_copy (second), third);

    g_paste_storage_backend_add_item (backend, name, third, history);

    {
        g_autolist (GPasteItem) items = NULL;
        gsize size;

        g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &items, &size));
        g_assert_cmpuint (g_list_length (items), ==, 2);
        g_assert_cmpstr (g_paste_item_get_value (items->next->data), ==, "second");
    }

    /* The same through a connection the failed write had to open. */
    g_autoptr (GPasteStorageBackend) reopened = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);

    g_paste_storage_backend_write_history (reopened, name, first);
    g_clear_object (&reopened);
    reopened = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    failed_sql = "PRAGMA journal_mode";
    g_paste_storage_backend_write_history (reopened, name, second);
    failed_sql = NULL;
    /* The open failed, rather than the case going by without it. */
    g_assert_true (g_paste_storage_backend_has_diverged (reopened, name));
    g_paste_storage_backend_add_item (reopened, name, third, history);

    g_autolist (GPasteItem) items = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (reopened, name, NULL, &items, &size));
    g_assert_cmpuint (g_list_length (items), ==, 2);
    g_assert_cmpstr (g_paste_item_get_value (items->next->data), ==, "second");
}

/* An add's reconciliation scans the store when its count fails: taken for a
 * count that matched, the failure would keep the row the add displaced. */
static void
test_sqlite_reconcile_uncounted (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    const gchar *name = "reconcile-uncounted";
    g_autoptr (GPasteItem) a = g_paste_text_item_new ("a");
    g_autoptr (GPasteItem) b = g_paste_text_item_new ("b");
    g_autoptr (GPasteItem) c = g_paste_text_item_new ("c");
    g_autoptr (GList) history = g_list_prepend (g_list_prepend (NULL, a), b);

    g_paste_storage_backend_write_history (backend, name, history);

    /* b displaced by c. */
    g_autoptr (GList) displaced = g_list_prepend (g_list_prepend (NULL, a), c);

    busy_step_sql = "SELECT COUNT (*) FROM items;";
    g_paste_storage_backend_add_item (backend, name, c, displaced);
    g_assert_null (busy_step_sql);

    g_autolist (GPasteItem) items = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &items, &size));
    g_assert_cmpuint (g_list_length (items), ==, 2);
}

/* A row the reconciliation failed to drop leaves the store behind the history
 * as much as a write that failed. */
static void
test_sqlite_reconcile_failed (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*failed to reconcile an item*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    const gchar *name = "reconcile-failed";
    g_autoptr (GPasteItem) a = g_paste_text_item_new ("a");
    g_autoptr (GPasteItem) b = g_paste_text_item_new ("b");
    g_autoptr (GPasteItem) c = g_paste_text_item_new ("c");
    g_autoptr (GList) history = g_list_prepend (g_list_prepend (NULL, a), b);

    g_paste_storage_backend_write_history (backend, name, history);

    g_autoptr (GList) displaced = g_list_prepend (g_list_prepend (NULL, a), c);

    busy_step_sql = "DELETE FROM items WHERE uuid = ?;";
    g_paste_storage_backend_add_item (backend, name, c, displaced);
    g_assert_null (busy_step_sql);
    g_assert_true (g_paste_storage_backend_has_diverged (backend, name));
}

/* A remove, a replace or an emptying that fails at one of its steps, given by
 * the statement failing there. */
typedef struct
{
    const gchar *operation;
    gboolean     unopened; /* through a backend whose connection fails to open */
    const gchar *failed_sql;
    const gchar *failed_prepare;
    const gchar *busy_step;
} IncrementalFailure;

#define G_PASTE_TEST_REPLACE_SQL "UPDATE items SET uuid = ?, kind = ?, value = ?, date = ?, checksum = ?, name = ?, image = ?, favourite = ?, timeout = ? WHERE uuid = ? RETURNING id;"

static const IncrementalFailure incremental_failures[] = {
    { "remove", TRUE, "PRAGMA journal_mode", NULL, NULL },
    { "remove", FALSE, NULL, "DELETE FROM items WHERE uuid = ?;", NULL },
    { "remove", FALSE, NULL, NULL, "DELETE FROM items WHERE uuid = ?;" },
    { "replace", TRUE, "PRAGMA journal_mode", NULL, NULL },
    { "replace", FALSE, "BEGIN IMMEDIATE", NULL, NULL },
    { "replace", FALSE, NULL, G_PASTE_TEST_REPLACE_SQL, NULL },
    { "replace", FALSE, NULL, NULL, G_PASTE_TEST_REPLACE_SQL },
    { "clear", FALSE, "DELETE FROM items;", NULL, NULL },
};

/* Whichever step of theirs fails, these leave the store behind the history as
 * much as a failed add: the item removed, or the whole history emptied, would
 * otherwise be back at the next start, nothing writing the history in full. */
static void
test_sqlite_incremental_failed (gconstpointer data)
{
    const IncrementalFailure *failure = data;

    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) writer = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autoptr (GPasteStorageBackend) unopened = (failure->unopened) ? g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings) : NULL;
    GPasteStorageBackend *backend = (unopened) ? unopened : writer;
    const gchar *name = "incremental-failed";
    g_autoptr (GPasteItem) a = g_paste_text_item_new ("a");
    g_autoptr (GPasteItem) b = g_paste_text_item_new ("b");
    g_autoptr (GPasteItem) c = g_paste_text_item_new ("c");
    g_autoptr (GList) history = g_list_prepend (g_list_prepend (NULL, a), b);

    g_paste_storage_backend_write_history (writer, name, history);

    failed_sql = failure->failed_sql;
    failed_prepare_sql = failure->failed_prepare;
    busy_step_sql = failure->busy_step;
    if (!g_strcmp0 (failure->operation, "remove"))
    {
        history = g_list_remove (history, a);
        g_paste_storage_backend_remove_item (backend, name, g_paste_item_get_uuid (a), history);
    }
    else if (!g_strcmp0 (failure->operation, "replace"))
    {
        g_list_find (history, a)->data = c;
        g_paste_storage_backend_replace_item (backend, name, g_paste_item_get_uuid (a), c, history);
    }
    else
        g_paste_storage_backend_clear_history (backend, name, NULL);
    failed_sql = NULL;

    g_assert_cmpuint (injected_failures, ==, (failure->failed_sql) ? 1 : 0);
    g_assert_null (failed_prepare_sql);
    g_assert_null (busy_step_sql);
    g_assert_true (g_paste_storage_backend_has_diverged (backend, name));
}

/* An add whose read of the top rank fails is a failed add: ranked as if the
 * store were empty, the newest item would be committed at its bottom -- the
 * rank the item removed here leaves free. */
static void
test_sqlite_unranked_add (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    const gchar *name = "unranked-add";
    g_autoptr (GPasteItem) a = g_paste_text_item_new ("a");
    g_autoptr (GPasteItem) b = g_paste_text_item_new ("b");
    g_autoptr (GPasteItem) c = g_paste_text_item_new ("c");
    g_autoptr (GList) history = g_list_prepend (g_list_prepend (NULL, a), b);

    g_paste_storage_backend_write_history (backend, name, history);
    history = g_list_remove (history, a);
    g_paste_storage_backend_remove_item (backend, name, g_paste_item_get_uuid (a), history);

    busy_step_sql = "SELECT COALESCE (MAX (rank), 0) FROM items;";
    g_paste_storage_backend_add_item (backend, name, c, NULL);
    g_assert_null (busy_step_sql);
    g_assert_true (g_paste_storage_backend_has_diverged (backend, name));
}

/* An image of noise, whose PNG is some kilobytes. */
static GPasteItem *
noisy_image_new (void)
{
    g_autoptr (GRand) rand = g_rand_new_with_seed (42);
    guchar pixels[32 * 32 * 4];

    for (gsize i = 0; i < sizeof (pixels); ++i)
        pixels[i] = g_rand_int_range (rand, 0, 256);

    g_autoptr (GBytes) bytes = g_bytes_new (pixels, sizeof (pixels));
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (32, 32, GDK_MEMORY_R8G8B8A8, bytes, 32 * 4);

    return g_paste_image_item_new (texture);
}

/* An item too large for a row is left out of the store, the rest of its
 * history written around it: refused, it would fail every write of that
 * history for as long as the history held it -- or, bound as the NULL a
 * refused bind leaves, store an image without its image. The same for a
 * replacement too large, which drops what it replaced, and for one of an
 * item's representations, the item stored without it. Any other refusal is a
 * failed write. */
static void
test_sqlite_item_too_large (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*too large to store*");
        g_test_trap_assert_stderr ("*failed to write an item*");
        g_test_trap_assert_stderr ("*failed to write a special value: out of memory*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    const gchar *name = "too-large";
    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (backend, name);
    g_autofree gchar *large = g_strnfill (2048, 'x');
    g_autoptr (GPasteItem) text = g_paste_text_item_new (large);
    g_autoptr (GPasteItem) image = noisy_image_new ();
    g_autoptr (GPasteItem) small = g_paste_text_item_new ("small");
    g_autoptr (GPasteItem) rich = g_paste_text_item_new ("rich");
    g_autoptr (GList) history = g_list_prepend (g_list_prepend (g_list_prepend (NULL, small), image), text);

    g_paste_settings_set_images_support (settings, TRUE);
    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML, g_bytes_new_take (g_strnfill (2048, 'y'), 2048)));
    refused_bind_length = 1024;

    g_paste_storage_backend_write_history (backend, name, history);
    g_assert_false (g_paste_storage_backend_has_diverged (backend, name));
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items;"), ==, 1);

    history = g_list_prepend (history, rich);
    g_paste_storage_backend_add_item (backend, name, rich, history);
    g_assert_false (g_paste_storage_backend_has_diverged (backend, name));
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items;"), ==, 2);
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM special_values;"), ==, 0);

    g_autoptr (GPasteItem) replacement = g_paste_text_item_new (large);

    g_list_find (history, small)->data = replacement;
    g_paste_storage_backend_replace_item (backend, name, g_paste_item_get_uuid (small), replacement, history);
    g_assert_false (g_paste_storage_backend_has_diverged (backend, name));
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items;"), ==, 1);

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* A password's name, which only the encrypted flavour stores. */
    g_autoptr (GPasteStorageBackend) encrypted = g_paste_sqlite_backend_new_encrypted (settings, "passphrase");
    g_autofree gchar *encrypted_path = g_paste_storage_backend_get_history_file_path (encrypted, name);
    g_autoptr (GPasteItem) password = g_paste_password_item_new (large, "secret", 0);
    g_autoptr (GPasteItem) kept = g_paste_text_item_new ("kept");
    g_autoptr (GList) passwords = g_list_prepend (g_list_prepend (NULL, kept), password);

    g_paste_storage_backend_write_history (encrypted, name, passwords);
    g_assert_false (g_paste_storage_backend_has_diverged (encrypted, name));
    g_assert_cmpint (sqlite_query_int (encrypted_path, "SELECT COUNT (*) FROM items;"), ==, 1);
#endif

    g_autoptr (GPasteItem) richer = g_paste_text_item_new ("richer");

    g_paste_item_add_special_value (richer, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML, g_bytes_new_take (g_strnfill (2048, 'z'), 2048)));
    refused_bind = SQLITE_NOMEM;
    g_paste_storage_backend_add_item (backend, name, image, NULL);
    g_assert_true (g_paste_storage_backend_has_diverged (backend, name));
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items;"), ==, 1);
    g_paste_storage_backend_add_item (backend, name, richer, NULL);
    refused_bind_length = 0;
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items;"), ==, 1);
}

static gboolean
items_counted (gconstpointer data,
               gconstpointer arg)
{
    return sqlite_query_int (data, "SELECT COUNT (*) FROM items;") == GPOINTER_TO_INT (arg);
}

/* A write the saver had out that fails leaves the store behind the history,
 * which then writes it in full, once: the adds after it displaced nothing, so
 * none carries the history that would bring the store level. A store refusing
 * every write is asked for that one full write, not for another after each
 * that fails -- and for one more when the history is flushed. */
static void
test_sqlite_diverged_history (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*injected disk-full failure*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "diverged-history";
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_SQLITE));

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_SQLITE);

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_assert_true (g_paste_storage_backend_try_lock ());
    g_paste_history_load (history, name);
    g_paste_history_add (history, g_paste_text_item_new ("a"));
    g_paste_test_bus_wait_until (items_counted, path, GINT_TO_POINTER (1));
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items;"), ==, 1);

    full_writes = 0;
    failed_sql = "BEGIN IMMEDIATE";
    failed_sql_once = TRUE;
    g_paste_history_add (history, g_paste_text_item_new ("b"));
    g_paste_history_add (history, g_paste_text_item_new ("c"));
    g_paste_test_bus_wait_until (items_counted, path, GINT_TO_POINTER (3));
    g_assert_null (failed_sql);
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items;"), ==, 3);
    g_assert_cmpuint (full_writes, ==, 1);

    injected_failures = 0;
    failed_sql = "BEGIN IMMEDIATE";
    failed_sql_once = FALSE;
    g_paste_history_add (history, g_paste_text_item_new ("d"));
    /* The add, and the full write after it: waited for, however slow the
     * worker, and then a moment for a third that must not come. */
    g_paste_test_bus_wait_for_count (&injected_failures, 2);
    g_paste_test_bus_pump (100);
    g_assert_cmpuint (injected_failures, ==, 2);

    /* Room again, and nothing copied since: the flush an exit makes writes
     * the store level, it being what the successor loads. */
    failed_sql = NULL;
    g_paste_history_flush (history);
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items;"), ==, 4);
}


/* A backup, or the emptying of another history, whose write fails answers
 * that it failed: the backup is not there to be listed, and the history
 * emptied is as it was, the images it holds included. The file flavour says
 * the same of a document it could not install. */
static void
test_sqlite_failed_write_answered (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*injected disk-full failure*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autofree gchar *path = g_paste_util_get_history_file_path ("answered", g_paste_storage_get_extension (G_PASTE_STORAGE_SQLITE));
    g_autofree gchar *backup_path = g_paste_util_get_history_file_path ("answered-backup", g_paste_storage_get_extension (G_PASTE_STORAGE_SQLITE));
    g_autofree gchar *images = g_paste_file_backend_images_dir ("answered");
    g_autofree gchar *image = g_build_filename (images, "kept.png", NULL);

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_SQLITE);

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("kept"));

        g_assert_true (g_paste_storage_backend_write_history (backend, "answered", items));
    }

    g_assert_cmpint (g_mkdir_with_parents (images, 0700), ==, 0);
    g_assert_true (g_file_set_contents (image, "", -1, NULL));

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    g_autoptr (GPasteDaemon3) skeleton = g_paste_daemon3_skeleton_new ();
    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .skeleton = skeleton };
    g_autoptr (GError) error = NULL;

    g_assert_true (g_paste_storage_backend_try_lock ());

    failed_sql = "DELETE FROM items;";
    g_paste_daemon_methods_backup_history (&methods, "answered", "answered-backup", &error);
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    g_clear_error (&error);
    empty_history (&methods, "answered", &error);
    failed_sql = NULL;
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);

    g_assert_false (g_file_test (backup_path, G_FILE_TEST_EXISTS));
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items;"), ==, 1);
    g_assert_true (g_file_test (image, G_FILE_TEST_EXISTS));

    g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("never installed"));
    static const gchar * const install[] = { ".xml.tmp", NULL };

    failed_moves = install;
    g_assert_false (g_paste_storage_backend_write_history (file, "answered-file", items));
    failed_moves = NULL;
    g_assert_true (g_paste_storage_backend_write_history (file, "answered-file", items));
}

/* Ranks past the compaction threshold are brought back down on open whatever
 * order the rows are in: one selected again holds a rank above older rows with
 * a higher id, which the compaction's unique index would otherwise trip on. */
static void
test_sqlite_rank_compaction (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "rank-compaction";
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_SQLITE));

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
        g_autolist (GPasteItem) items = NULL;

        items = g_list_append (items, g_paste_text_item_new ("first"));
        items = g_list_append (items, g_paste_text_item_new ("second"));
        items = g_list_append (items, g_paste_text_item_new ("third"));
        g_paste_storage_backend_write_history (backend, name, items);
    }

    {
        g_autoptr (sqlite3) db = NULL;

        /* The oldest row selected again past the threshold. The write inserted
         * the list front first, so 'first' has the lowest id and rank 3: the
         * compaction moves it to rank 2 while 'second' still holds it. */
        g_assert_cmpint (sqlite3_open (path, &db), ==, SQLITE_OK);
        g_assert_cmpint (sqlite3_exec (db, "UPDATE items SET rank = 4611686018427387914 WHERE value = 'third';", NULL, NULL, NULL), ==, SQLITE_OK);
    }

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autolist (GPasteItem) items = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &items, &size));
    g_assert_cmpuint (g_list_length (items), ==, 3);
    g_assert_cmpstr (g_paste_item_get_value (items->data), ==, "third");
    g_clear_object (&backend);
    g_assert_cmpint (sqlite_query_int (path, "SELECT MAX (rank) FROM items;"), ==, 3);
}

/* A destination already holding the history in a form this build cannot read
 * -- a newer GPaste's database -- is one the import stops before, rather than
 * one its rollback deletes after the write refused to touch it. */
static void
test_import_spares_unreadable_destination (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*already holds a history*it cannot read*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    g_autofree gchar *database = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_SQLITE));

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);
    g_paste_settings_set_storage_backend_revision (settings, G_PASTE_STORAGE_BACKEND_REVISION - 1);

    {
        g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("source"));
        g_autoptr (sqlite3) db = NULL;

        g_paste_storage_backend_write_history (file, name, items);
        write_version_1 (database);
        g_assert_cmpint (sqlite3_open (database, &db), ==, SQLITE_OK);
        g_assert_cmpint (sqlite3_exec (db, "PRAGMA user_version = 99;", NULL, NULL, NULL), ==, SQLITE_OK);
    }

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GError) error = NULL;

    prompt->chosen = G_PASTE_STORAGE_SQLITE;
    prompt->import = TRUE;
    prompt->cleanup = TRUE;
    g_assert_true (run_migration (settings, prompt, &error));
    g_assert_no_error (error);

    assert_import_failure_reported (prompt);
    g_assert_cmpint (g_paste_settings_get_storage_backend (settings), ==, G_PASTE_STORAGE_FILE);
    g_assert_true (g_file_test (database, G_FILE_TEST_EXISTS));
    g_assert_cmpint (sqlite_query_int (database, "PRAGMA user_version;"), ==, 99);
}

/* A destination history whose write failed, installing nothing, is one the
 * rollback leaves as it was: an older copy an earlier migration kept stays. */
static void
test_import_spares_unwritten_destination (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*injected failure moving*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    const gchar *name = "unwritten";

    {
        g_autoptr (GPasteStorageBackend) sqlite = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
        g_autolist (GPasteItem) old = g_list_append (NULL, g_paste_text_item_new ("older copy"));
        g_autolist (GPasteItem) source = g_list_append (NULL, g_paste_text_item_new ("source"));

        g_assert_true (g_paste_storage_backend_write_history (file, name, old));
        g_assert_true (g_paste_storage_backend_write_history (sqlite, name, source));
    }

    static const gchar * const install[] = { ".xml.tmp", NULL };
    g_autoptr (GError) error = NULL;

    failed_moves = install;
    g_assert_false (import_histories (settings, G_PASTE_STORAGE_SQLITE, NULL, G_PASTE_STORAGE_FILE, NULL, &error));
    failed_moves = NULL;
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);

    g_autolist (GPasteItem) items = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (file, name, NULL, &items, &size));
    g_assert_cmpuint (g_list_length (items), ==, 1);
    g_assert_cmpstr (g_paste_item_get_value (items->data), ==, "older copy");
}

/* A version 1 database whose upgrade fails at the statement @data names has
 * to come out of it exactly as it went in, so that the next open upgrades it
 * whole. */
static void
test_schema_failure (gconstpointer data)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*injected disk-full failure*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "schema-failure";
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_SQLITE));

    write_version_1 (path);
    failed_sql = data;
    g_assert_false (read_version_1 (settings, name));
    failed_sql = NULL;

    g_assert_cmpint (sqlite_query_int (path, "PRAGMA user_version;"), ==, 1);
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM pragma_table_info ('items') WHERE name IN ('favourite', 'timeout');"), ==, 0);
    g_assert_true (read_version_1 (settings, name));
}

/* A version that cannot be read is no version 0: the open is refused rather
 * than a schema created over the old tables and stamped current, which no
 * later open would have upgraded. */
static void
test_schema_version_unreadable (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*could not read the schema version*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "schema-version-unreadable";
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_SQLITE));

    write_version_1 (path);
    busy_step_sql = "PRAGMA user_version;";
    g_assert_false (read_version_1 (settings, name));
    busy_step_sql = NULL;

    g_assert_cmpint (sqlite_query_int (path, "PRAGMA user_version;"), ==, 1);
    g_assert_true (read_version_1 (settings, name));

    /* The same of the read the upgrade makes again under the write lock. */
    const gchar *locked_name = "schema-version-unreadable-locked";
    g_autofree gchar *locked_path = g_paste_util_get_history_file_path (locked_name, g_paste_storage_get_extension (G_PASTE_STORAGE_SQLITE));

    write_version_1 (locked_path);
    busy_step_sql = "PRAGMA user_version;";
    busy_step_skip = 1;
    g_assert_false (read_version_1 (settings, locked_name));
    g_assert_null (busy_step_sql);

    g_assert_cmpint (sqlite_query_int (locked_path, "PRAGMA user_version;"), ==, 1);
    g_assert_true (read_version_1 (settings, locked_name));
}

/* Another GPaste upgrading the database between the version read and the
 * transaction leaves this one nothing to do, not the same steps to fail on. */
static void
test_schema_upgraded_meanwhile (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "schema-upgraded-meanwhile";
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_SQLITE));

    write_version_1 (path);
    upgraded_meanwhile = path;
    g_assert_true (read_version_1 (settings, name));
    g_assert_null (upgraded_meanwhile);
    g_assert_cmpint (sqlite_query_int (path, "PRAGMA user_version;"), ==, 3);
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

/* An encrypted SQLite history holding data, whose count cannot be read when a
 * passphrase it refuses comes: refused, as one known to hold data is, rather
 * than taken for empty and keyed afresh over what its rows were encrypted
 * with. */
static void
test_uncounted_crypto (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*does not unlock*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "uncounted-crypto";

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

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, "wrong");
        g_autolist (GPasteItem) items = NULL;
        gsize size;

        busy_step_sql = "SELECT COUNT (*) FROM items;";
        g_assert_false (g_paste_storage_backend_read_history (backend, name, NULL, &items, &size));
        g_assert_null (busy_step_sql);
    }

    g_autofree gchar *kept_salt = read_meta (db, "salt");
    g_autofree gchar *kept_check = read_meta (db, "check");

    g_assert_cmpstr (kept_salt, ==, salt);
    g_assert_cmpstr (kept_check, ==, check);
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

    /* And so is asking whether it holds anything to verify against. */
    closer = hold_busy (path);
    g_assert_true (g_paste_storage_passphrase_verifiable (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings));
    release_busy (closer);
}

/* An empty active database proves nothing: verifying reads it without keying
 * it afresh for the passphrase under test, as opening it would, and the
 * histories holding data decide. Nothing in the store changes to find out. */
static void
test_verification_empty_active_database (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *active = g_paste_settings_get_history_name (settings);

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, "passphrase");
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("precious"));

        g_paste_storage_backend_write_history (backend, "beside-the-empty-one", items);
        g_paste_storage_backend_write_history (backend, active, NULL);
    }

    g_autofree gchar *path = g_paste_util_get_history_file_path (active, g_paste_storage_get_extension (G_PASTE_STORAGE_ENCRYPTED_SQLITE));
    g_autofree gchar *before = NULL;
    g_autofree gchar *after = NULL;
    gsize before_length = 0;
    gsize after_length = 0;

    g_assert_true (g_file_get_contents (path, &before, &before_length, NULL));
    g_assert_false (g_paste_storage_passphrase_can_decrypt (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings, "typo"));
    g_assert_true (g_paste_storage_passphrase_can_decrypt (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings, "passphrase"));
    g_assert_true (g_file_get_contents (path, &after, &after_length, NULL));
    g_assert_cmpmem (before, before_length, after, after_length);
}
#endif

/* A lock file that cannot be opened -- a directory at its name here, a file
 * another user owns or a read-only data directory elsewhere -- tells no daemon
 * apart, nor does one flock () refuses: the store may be touched all the
 * same, rather than every load reading nothing and every Backup, Empty or
 * Delete blaming another daemon. */
static void
test_lock_file_unusable (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*Failed to open the history lock file: *");
        g_test_trap_assert_stderr ("*Failed to lock the history: *");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autofree gchar *dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *lock = g_build_filename (dir, "lock", NULL);

    g_assert_true (g_paste_util_ensure_history_dir_exists ());
    g_assert_cmpint (g_mkdir (lock, 0700), ==, 0);
    g_assert_true (g_paste_storage_backend_try_lock ());
    g_assert_true (g_paste_storage_backend_lock (NULL));

    /* The same of one flock () refuses. */
    g_assert_cmpint (g_rmdir (lock), ==, 0);
    refused_flock = ENOLCK;
    g_assert_true (g_paste_storage_backend_try_lock ());
    g_assert_true (g_paste_storage_backend_lock (NULL));
    refused_flock = 0;
}

/* Nor does a history directory that cannot be made, which says so itself: the
 * lock file was never tried, and errno, once that is logged, says nothing of
 * it. */
static void
test_lock_dir_unmade (void)
{
    if (geteuid () == 0)
    {
        g_test_skip ("A directory's mode stops no write of root's");
        return;
    }

    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*Could not create history dir*");
        g_test_trap_assert_stderr_unmatched ("*history lock file*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_ERROR);

    g_autofree gchar *dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *parent = g_path_get_dirname (dir);

    g_assert_false (g_file_test (dir, G_FILE_TEST_EXISTS));
    g_assert_cmpint (g_mkdir_with_parents (parent, 0500), ==, 0);
    g_assert_cmpint (g_chmod (parent, 0500), ==, 0);
    g_assert_true (g_paste_storage_backend_try_lock ());
    g_assert_true (g_paste_storage_backend_lock (NULL));
    g_assert_cmpint (g_chmod (parent, 0700), ==, 0);
}

/* A full write that does not land sweeps nothing: what is on disk is still the
 * snapshot before it, which references the image the failed one dropped. */
static void
test_failed_write_keeps_images (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*Failed to install history file*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    const gchar *name = "failed-write-images";
    const guchar pixel[] = { 50, 60, 70, 255 };
    g_autoptr (GBytes) bytes = g_bytes_new (pixel, sizeof (pixel));
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (1, 1, GDK_MEMORY_R8G8B8A8, bytes, 4);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_image_item_new (texture));
    g_autofree gchar *image = g_paste_file_backend_image_path (name, g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (items->data)));
    const gchar * const history_install[] = { ".xml.tmp", NULL };

    g_paste_settings_set_images_support (settings, TRUE);
    g_paste_storage_backend_write_history (backend, name, items);
    g_assert_true (g_file_test (image, G_FILE_TEST_EXISTS));

    failed_moves = history_install;
    g_paste_storage_backend_write_history (backend, name, NULL);
    failed_moves = NULL;

    g_autolist (GPasteItem) kept = NULL;
    gsize size;

    g_assert_true (g_file_test (image, G_FILE_TEST_EXISTS));
    g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &kept, &size));
    g_assert_cmpuint (g_list_length (kept), ==, 1);
}

/* An image under a history whose name is not UTF-8 -- one read off the disk,
 * or a home directory in a legacy 8-bit encoding -- has a path no document can
 * hold: it is named by its file name alone, and read back from the history's
 * images directory, rather than left out of the write or taking the daemon down
 * with it. */
static void
test_image_name_not_utf8 (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    const gchar *name = "d\xe9" "sir\xe9" "e";
    const guchar pixel[] = { 52, 62, 72, 255 };
    g_autoptr (GBytes) bytes = g_bytes_new (pixel, sizeof (pixel));
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (1, 1, GDK_MEMORY_R8G8B8A8, bytes, 4);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_image_item_new (texture));
    g_autofree gchar *checksum = g_strdup (g_paste_image_item_get_checksum (items->data));

    items = g_list_append (items, g_paste_text_item_new ("saved"));
    g_paste_settings_set_images_support (settings, TRUE);
    g_assert_true (g_paste_storage_backend_write_history (backend, name, items));

    g_autolist (GPasteItem) loaded = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &loaded, &size));
    g_assert_cmpuint (g_list_length (loaded), ==, 2);
    g_assert_true (G_PASTE_IS_IMAGE_ITEM (loaded->data));
    g_assert_cmpstr (g_paste_image_item_get_checksum (loaded->data), ==, checksum);
    g_assert_cmpstr (g_paste_item_get_value (loaded->next->data), ==, "saved");
}

/* A history written without one of its images does not hold what it was
 * given: the write says so -- a backup missing its screenshots is no backup --
 * and one that was not there before is not left behind. */
static void
test_image_write_failed (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*Failed to create images directory*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    const gchar *name = "image-write-failed";
    const guchar pixel[] = { 82, 83, 84, 255 };
    g_autoptr (GBytes) bytes = g_bytes_new (pixel, sizeof (pixel));
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (1, 1, GDK_MEMORY_R8G8B8A8, bytes, 4);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_image_item_new (texture));
    g_autofree gchar *images = g_paste_file_backend_images_dir (name);
    g_autofree gchar *document = g_paste_util_get_history_file_path (name, "xml");
    g_autofree gchar *parent = g_path_get_dirname (images);

    g_paste_settings_set_images_support (settings, TRUE);
    /* What the image's directory cannot be made through. */
    g_assert_cmpint (g_mkdir_with_parents (parent, 0700), ==, 0);
    g_assert_true (g_file_set_contents (images, "", 0, NULL));

    g_assert_false (g_paste_storage_backend_write_history (backend, name, items));
    g_assert_false (g_file_test (document, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (images, G_FILE_TEST_IS_REGULAR));
}

/* A write of a history that was not there failing past its images -- the
 * document refusing to go in -- takes back the images it wrote: no listing
 * shows a name no history has, and nothing would ever reach them again. */
static void
test_new_history_write_failed (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*Failed to install history file*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    const gchar *name = "new-history-write-failed";
    const guchar pixel[] = { 85, 86, 87, 255 };
    g_autoptr (GBytes) bytes = g_bytes_new (pixel, sizeof (pixel));
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (1, 1, GDK_MEMORY_R8G8B8A8, bytes, 4);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_image_item_new (texture));
    g_autofree gchar *images = g_paste_file_backend_images_dir (name);
    g_autofree gchar *document = g_paste_util_get_history_file_path (name, "xml");
    g_autofree gchar *blocker = g_build_filename (document, "blocker", NULL);

    g_paste_settings_set_images_support (settings, TRUE);
    /* What the document cannot be moved over. */
    g_assert_cmpint (g_mkdir_with_parents (document, 0700), ==, 0);
    g_assert_true (g_file_set_contents (blocker, "", 0, NULL));

    g_assert_false (g_paste_storage_backend_write_history (backend, name, items));
    g_assert_false (g_file_test (images, G_FILE_TEST_EXISTS));
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* Checking a passphrase against the active history decrypts its document --
 * once to see whether the key refutes it, once to see whether it confirms it
 * -- and none of its images, each a stream salted apart, a derivation of its
 * own: on the thread the unlock runs on, the compositor's inside GNOME Shell,
 * each is a stall. */
static void
test_passphrase_verify_cost (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);
    g_paste_settings_set_history_name (settings, "verify-cost");

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (G_PASTE_STORAGE_ENCRYPTED_FILE, settings, "passphrase");
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("text"));

        for (guchar i = 0; i < 3; ++i)
        {
            const guchar pixel[4] = { i, 2 * i, 3 * i, 255 };
            g_autoptr (GBytes) data = g_bytes_new (pixel, sizeof (pixel));
            g_autoptr (GdkTexture) texture = gdk_memory_texture_new (1, 1, GDK_MEMORY_R8G8B8A8, data, 4);

            items = g_list_append (items, g_paste_image_item_new (texture));
        }

        g_paste_storage_backend_write_history (backend, "verify-cost", items);
    }

    g_atomic_int_set (&derivations, 0);
    g_assert_true (g_paste_storage_passphrase_can_decrypt (G_PASTE_STORAGE_ENCRYPTED_FILE, settings, "passphrase"));
    g_assert_cmpint (g_atomic_int_get (&derivations), ==, 2);
    g_paste_settings_reset (settings, G_PASTE_HISTORY_NAME_SETTING);
}
#endif

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* Only a key that opens nothing refutes a passphrase: a history damaged past its
 * first chunk, or a foreign file under the flavour's name, leaves the right one
 * accepted, where it would otherwise be refused for every history. */
static void
test_passphrase_survives_damage (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (G_PASTE_STORAGE_ENCRYPTED_FILE, settings, "passphrase");
    g_autofree gchar *long_text = g_strnfill (3 * 4096, 'x');
    g_autolist (GPasteItem) intact = g_list_append (NULL, g_paste_text_item_new ("intact"));
    g_autolist (GPasteItem) damaged = g_list_append (NULL, g_paste_text_item_new (long_text));

    g_paste_storage_backend_write_history (backend, "intact", intact);
    g_paste_storage_backend_write_history (backend, "damaged", damaged);

    g_autofree gchar *damaged_path = g_paste_util_get_history_file_path ("damaged", "xmls");
    g_autofree gchar *foreign_path = g_paste_util_get_history_file_path ("foreign", "xmls");
    g_autofree gchar *contents = NULL;
    gsize length = 0;
    /* The stream header, the first chunk's length and ciphertext, and the
     * second's length: what follows is the second chunk's ciphertext. */
    const gsize second_chunk = 64 + 4 + 4096 + 17 + 4;

    g_assert_true (g_file_get_contents (damaged_path, &contents, &length, NULL));
    g_assert_cmpuint (length, >, second_chunk + 16);
    contents[second_chunk + 16] ^= 1;
    g_assert_true (g_file_set_contents (damaged_path, contents, (gssize) length, NULL));
    g_assert_true (g_file_set_contents (foreign_path, "a file of somebody else's, long enough to hold a stream header and then some", -1, NULL));

    g_assert_true (g_paste_storage_passphrase_can_decrypt (G_PASTE_STORAGE_ENCRYPTED_FILE, settings, "passphrase"));
    g_assert_false (g_paste_storage_passphrase_can_decrypt (G_PASTE_STORAGE_ENCRYPTED_FILE, settings, "wrong passphrase"));
}
#endif

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* A history the user may not read refutes nothing either: its open fails with
 * the error the converter answers a wrong key with. */
static void
test_passphrase_survives_unreadable (void)
{
    if (!geteuid ())
    {
        g_test_skip ("root reads a file of mode 0");
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (G_PASTE_STORAGE_ENCRYPTED_FILE, settings, "passphrase");
    g_autolist (GPasteItem) intact = g_list_append (NULL, g_paste_text_item_new ("intact"));
    g_autolist (GPasteItem) locked = g_list_append (NULL, g_paste_text_item_new ("locked"));

    g_paste_storage_backend_write_history (backend, "intact", intact);
    g_paste_storage_backend_write_history (backend, "locked", locked);

    g_autofree gchar *locked_path = g_paste_util_get_history_file_path ("locked", "xmls");

    g_assert_cmpint (g_chmod (locked_path, 0), ==, 0);
    gboolean right = g_paste_storage_passphrase_can_decrypt (G_PASTE_STORAGE_ENCRYPTED_FILE, settings, "passphrase");
    gboolean wrong = g_paste_storage_passphrase_can_decrypt (G_PASTE_STORAGE_ENCRYPTED_FILE, settings, "wrong passphrase");
    g_assert_cmpint (g_chmod (locked_path, 0600), ==, 0);

    g_assert_true (right);
    g_assert_false (wrong);
}
#endif

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* A history is created only under a name the storage in use can keep every file
 * of, a switch's, a backup's, an emptying's and an import's alike: an encrypted
 * file history past 240 bytes could never have its passphrase changed. So
 * while that store is locked, the daemon running on no storage over it. */
static void
test_new_history_name_fits (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_storage_backend_set_passphrase ("passphrase");
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    g_autoptr (GPasteDaemon3) skeleton = g_paste_daemon3_skeleton_new ();
    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .skeleton = skeleton };
    g_autofree gchar *name = g_strnfill (241, 'n');
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "xmls");

    {
        g_autoptr (GPasteStorageBackend) plain = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("plain"));
        g_autoptr (GError) error = NULL;

        g_assert_true (g_paste_storage_backend_write_history (plain, name, items));
        g_assert_false (import_histories (settings, G_PASTE_STORAGE_FILE, NULL, G_PASTE_STORAGE_ENCRYPTED_FILE, "passphrase", &error));
        g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
        g_paste_storage_backend_delete_history (plain, name, NULL);
    }

    for (guint locked = 0; locked < 2; ++locked)
    {
        g_autoptr (GError) error = NULL;

        /* No passphrase in the process, as when the unlock was dismissed. */
        if (locked)
            g_paste_storage_backend_set_passphrase (NULL);

        g_paste_daemon_methods_switch_history (&methods, name, &error);
        g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT);
        g_clear_error (&error);
        g_paste_daemon_methods_backup_history (&methods, "history", name, &error);
        g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT);
        g_clear_error (&error);
        empty_history (&methods, name, &error);
        g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT);
        g_assert_cmpstr (g_paste_history_get_current (history), !=, name);
        g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));
    }
}

static void
secure_tracking_start (void)
{
    G_LOCK (secure);
    secure_sizes = g_hash_table_new (NULL, NULL);
    secure_tracked = TRUE;
    secure_frees = unwiped_frees = 0;
    G_UNLOCK (secure);
}

static void
secure_tracking_stop (void)
{
    G_LOCK (secure);
    secure_tracked = FALSE;
    g_clear_pointer (&secure_sizes, g_hash_table_unref);
    G_UNLOCK (secure);

    g_assert_cmpuint (secure_frees, >, 0);
    g_assert_cmpuint (unwiped_frees, ==, 0);
}

/* @storage's key, and for the file flavour its stream's state and buffers,
 * are wiped before gcr gets them back, whichever memory it handed out. */
static void
test_secure_memory_wiped (gconstpointer data)
{
    GPasteStorage storage = GPOINTER_TO_UINT (data);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("secret"));

    secure_tracking_start ();
    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (storage, settings, "passphrase");
        g_autolist (GPasteItem) loaded = NULL;
        gsize size;

        g_assert_true (g_paste_storage_backend_write_history (backend, "wiped", items));
        g_assert_true (g_paste_storage_backend_read_history (backend, "wiped", NULL, &loaded, &size));
        g_assert_cmpuint (g_list_length (loaded), ==, 1);
    }
    secure_tracking_stop ();
}

/* The same of a stream given up on halfway, its plaintext still buffered: on
 * the way in when encrypting, on the way out when decrypting. */
static void
test_secure_memory_wiped_halfway (void)
{
    guint8 stream[1024];
    gsize read, written;
    g_autoptr (GError) error = NULL;

    {
        g_autoptr (GConverter) encrypt = g_paste_secret_stream_converter_new (G_PASTE_SECRET_STREAM_ENCRYPT, "passphrase");

        g_assert_cmpint (g_converter_convert (encrypt, "secret", 6, stream, sizeof (stream), G_CONVERTER_INPUT_AT_END, &read, &written, &error), ==, G_CONVERTER_FINISHED);
        g_assert_no_error (error);
    }

    gsize length = written;
    guint8 first;

    secure_tracking_start ();
    {
        g_autoptr (GConverter) encrypt = g_paste_secret_stream_converter_new (G_PASTE_SECRET_STREAM_ENCRYPT, "passphrase");
        guint8 header[1024];

        g_assert_cmpint (g_converter_convert (encrypt, "secret", 6, header, sizeof (header), G_CONVERTER_NO_FLAGS, &read, &written, &error), ==, G_CONVERTER_CONVERTED);
        g_assert_cmpuint (read, ==, 6);
    }
    {
        g_autoptr (GConverter) decrypt = g_paste_secret_stream_converter_new (G_PASTE_SECRET_STREAM_DECRYPT, "passphrase");

        g_assert_cmpint (g_converter_convert (decrypt, stream, length, &first, 1, G_CONVERTER_INPUT_AT_END, &read, &written, &error), ==, G_CONVERTER_CONVERTED);
        g_assert_cmpuint (written, ==, 1);
        g_assert_cmpint (first, ==, 's');
    }
    secure_tracking_stop ();
}
#endif

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* An encrypted history goes through the converter's fixed buffers a frame at a
 * time, many chunks of it reading back whole; carried on past its FINAL frame
 * -- bytes appended, or a second stream after the first -- it is refused rather
 * than read as its first stream alone. */
static void
test_encrypted_stream_bounds (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*Data past the end of the encrypted stream*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "passphrase");
    const gchar *name = "stream-bounds";
    g_autofree gchar *long_text = g_strnfill (5 * 4096 + 123, 'y');
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new (long_text));
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "xmls");
    g_autofree gchar *contents = NULL;
    gsize length = 0;
    gsize size;

    g_paste_storage_backend_write_history (backend, name, items);

    {
        g_autolist (GPasteItem) loaded = NULL;

        g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &loaded, &size));
        g_assert_cmpuint (g_list_length (loaded), ==, 1);
        g_assert_cmpstr (g_paste_item_get_value (loaded->data), ==, long_text);
    }

    g_assert_true (g_file_get_contents (path, &contents, &length, NULL));

    g_autofree gchar *twice = g_malloc (2 * length);

    memcpy (twice, contents, length);
    memcpy (twice + length, contents, length);

    const gsize extended_lengths[] = { length + 7, 2 * length };

    for (guint i = 0; i < G_N_ELEMENTS (extended_lengths); ++i)
    {
        g_autolist (GPasteItem) loaded = NULL;

        g_assert_true (g_file_set_contents (path, twice, (gssize) extended_lengths[i], NULL));
        g_assert_false (g_paste_storage_backend_read_history (backend, name, NULL, &loaded, &size));
    }
}
#endif

/* The storage lock held by another process, as a daemon still writing holds it:
 * an open file of our own, which flock () tells apart from the one the lock
 * would open even in this process. */
static gint
hold_storage_lock (void)
{
    g_autofree gchar *dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *lock = g_build_filename (dir, "lock", NULL);

    g_assert_true (g_paste_util_ensure_history_dir_exists ());

    gint fd = g_open (lock, O_RDWR | O_CREAT | O_CLOEXEC, 0600);

    g_assert_cmpint (fd, >=, 0);
    g_assert_cmpint (flock (fd, LOCK_EX | LOCK_NB), ==, 0);

    return fd;
}

typedef struct
{
    GCancellable *cancellable;
    gint          done;
    gboolean      locked;
} LockWait;

static gpointer
wait_for_lock (gpointer data)
{
    LockWait *wait = data;

    wait->locked = g_paste_storage_backend_lock (wait->cancellable);
    g_atomic_int_set (&wait->done, TRUE);

    return NULL;
}

/* Two waits of one process -- a load and the one superseding it -- both get the
 * lock once its holder lets go: the second finds the first holds it, rather
 * than have its own flock () refused for good by its own process. */
static void
test_storage_lock_two_waits (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    gint held = hold_storage_lock ();
    LockWait waits[2] = { { g_cancellable_new (), FALSE, FALSE }, { g_cancellable_new (), FALSE, FALSE } };
    GThread *threads[2];

    for (guint i = 0; i < 2; ++i)
        threads[i] = g_thread_new ("lock-wait", wait_for_lock, &waits[i]);

    g_usleep (G_USEC_PER_SEC / 4);
    g_close (held, NULL);

    gint64 deadline = g_get_monotonic_time () + 3 * G_USEC_PER_SEC;

    while ((!g_atomic_int_get (&waits[0].done) || !g_atomic_int_get (&waits[1].done)) && g_get_monotonic_time () < deadline)
        g_usleep (G_USEC_PER_SEC / 20);

    for (guint i = 0; i < 2; ++i)
    {
        g_cancellable_cancel (waits[i].cancellable);
        g_thread_join (threads[i]);
        g_object_unref (waits[i].cancellable);
        g_assert_true (waits[i].locked);
    }
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

/* The lock a wait of this process has just taken is the process's: asked
 * then -- a Backup, an Empty or a Delete -- try_lock () is not refused by the
 * process's own lock, as if another daemon held it. */
static void
test_taken_lock_is_ours (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    gint held = hold_storage_lock ();

    g_paste_history_load_async (history, "taken");
    g_atomic_int_set (&pause_taken_lock, TRUE);
    g_close (held, NULL);

    g_mutex_lock (&lock_taken_mutex);
    while (!lock_taken)
        g_cond_wait (&lock_taken_cond, &lock_taken_mutex);
    g_mutex_unlock (&lock_taken_mutex);

    g_assert_true (g_paste_storage_backend_try_lock ());
    wait_load (history);
}

/* A load still waiting for the lock when its own process lets go of it --
 * standing down for a takeover -- reads nothing: the store is still the other
 * daemon's, which may be writing it. */
static void
test_load_after_release_reads_nothing (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*Could not read the history back*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "released-wait";

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("written elsewhere"));

        g_paste_storage_backend_write_history (backend, name, items);
    }

    gint held = hold_storage_lock ();
    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    busy_flocks = 0;
    g_paste_history_load_async (history, name);
    /* Its wait under way before the release, however late its worker started. */
    g_paste_test_bus_wait_for_count (&busy_flocks, 1);
    g_paste_storage_backend_unlock ();
    wait_load (history);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);
    g_close (held, NULL);
}

/* A history dropped while its load waits for the lock goes there and then, and
 * the wait goes with it: nothing takes the lock once its holder lets go. */
static void
test_dropped_history_ends_its_load (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    gint held = hold_storage_lock ();
    GPasteHistory *history = g_paste_history_new (settings);

    g_object_add_weak_pointer (G_OBJECT (history), (gpointer *) &history);
    g_paste_history_load_async (history, "dropped-loading");
    g_usleep (G_USEC_PER_SEC / 4);
    g_object_unref (history);
    g_assert_null (history);
    g_paste_test_bus_pump (500);
    g_close (held, NULL);
    g_paste_test_bus_pump (500);
    g_close (hold_storage_lock (), NULL);
}

/* A history handed over -- flushed, its process's lock released -- starts no
 * load when its name changes, which would take the lock back from the daemon it
 * was handed to; resuming starts it, so that the history recorded into from
 * then on is the one switched to and not the model the handover left. */
static void
test_flushed_history_loads_nothing (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("switched to"));

        g_paste_storage_backend_write_history (backend, "switched-meanwhile", items);
    }

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load (history, "handed-over");
    g_paste_history_flush (history);
    g_paste_storage_backend_unlock ();
    loads_started = 0;
    g_paste_settings_set_history_name (settings, "switched-meanwhile");
    g_assert_cmpuint (loads_started, ==, 0);
    g_close (hold_storage_lock (), NULL);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);

    g_paste_history_resume (history);
    g_assert_cmpuint (loads_started, ==, 1);
    wait_load (history);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
    /* The write the load asks for lands before the test's directories go. */
    g_paste_history_flush (history);
}

/* A load out when the store is handed over is superseded by a switch made then,
 * though the switch's own load waits for the handover to end: the history left
 * is not installed as the one switched to. */
static void
test_flushed_switch_supersedes_load (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("item of the history left"));

        g_paste_storage_backend_write_history (backend, "load-left", items);
    }

    gint held = hold_storage_lock ();
    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load_async (history, "load-left");
    g_assert_true (g_paste_history_is_loading (history));
    g_paste_history_flush (history);
    g_paste_settings_set_history_name (settings, "switched-while-loading");
    g_close (held, NULL);
    g_paste_test_bus_pump (500);
    g_assert_cmpstr (g_paste_history_get_current (history), ==, "switched-while-loading");
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);

    g_paste_history_resume (history);
    wait_load (history);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);
    g_paste_settings_reset (settings, G_PASTE_HISTORY_NAME_SETTING);
    g_paste_history_flush (history);
}

/* A switch reads the history switched to and writes nothing back: a write would
 * be the whole history rewritten, re-encrypted and synced, for every switch. */
static void
test_switch_writes_nothing (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("read"));

        g_paste_storage_backend_write_history (backend, "switched-unwritten", items);
    }

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load (history, "switched-from");
    g_clear_pointer (&synced, g_hash_table_unref);
    g_assert_true (g_paste_history_switch (history, "switched-unwritten"));
    wait_load (history);
    g_paste_test_bus_pump (200);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);

    g_autoptr (GList) synced_paths = (synced) ? g_hash_table_get_keys (synced) : NULL;

    for (const GList *l = synced_paths; l; l = l->next)
        g_assert_false (g_str_has_suffix (l->data, "switched-unwritten.xml.tmp"));

    g_paste_settings_reset (settings, G_PASTE_HISTORY_NAME_SETTING);
    g_paste_history_flush (history);
}

/* Read @name back through a plain file backend. */
static guint
stored_length (GPasteSettings *settings,
               const gchar    *name)
{
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autolist (GPasteItem) items = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &items, &size));

    return g_list_length (items);
}

/* Until this process holds the storage lock, the daemon it takes over from may
 * still be writing any history down: backing another one up, emptying or
 * deleting it is refused, as is emptying the current one while its load waits,
 * and each goes through once the lock is ours. */
static void
test_store_methods_wait_for_lock (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("kept"));

        g_paste_storage_backend_write_history (backend, "other", items);
    }

    gint held = hold_storage_lock ();
    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    g_autoptr (GPasteDaemon3) skeleton = g_paste_daemon3_skeleton_new ();
    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .skeleton = skeleton };
    g_autoptr (GError) error = NULL;

    g_paste_history_load_async (history, "waiting");

    g_paste_daemon_methods_backup_history (&methods, "other", "other-backup", &error);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
    g_clear_error (&error);
    empty_history (&methods, "other", &error);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
    g_clear_error (&error);
    g_paste_daemon_methods_delete_history (&methods, "other", &error);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
    g_clear_error (&error);
    empty_history (&methods, "waiting", &error);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
    g_clear_error (&error);
    g_assert_true (g_paste_history_is_loading (history));
    g_assert_cmpuint (stored_length (settings, "other"), ==, 1);

    g_close (held, NULL);
    wait_load (history);
    g_paste_daemon_methods_backup_history (&methods, "other", "other-backup", &error);
    g_assert_no_error (error);
    g_paste_daemon_methods_delete_history (&methods, "other", &error);
    g_assert_no_error (error);
    /* What the backup and the load ask for lands before the test's
     * directories go. */
    g_paste_history_flush (history);
}

/* Emptying reaches the store whatever state the history is in: one still
 * loading would have what the load read installed right after, and one that
 * could not be read would keep every item -- either way they were back at the
 * next start. Not before the daemon taken over from is done writing it, though:
 * refused until then, the load left to land. */
static void
test_empty_reaches_the_store (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*Could not read the history back*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("one"));

        items = g_list_append (items, g_paste_text_item_new ("two"));
        g_paste_storage_backend_write_history (backend, "emptied-loading", items);
    }

    {
        gint held = hold_storage_lock ();
        g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

        g_autoptr (GError) error = NULL;

        g_paste_history_load_async (history, "emptied-loading");
        g_assert_false (g_paste_history_empty (history, &error));
        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
        g_assert_true (g_paste_history_is_loading (history));
        g_close (held, NULL);
        g_assert_true (g_paste_history_empty (history, NULL));
        wait_load (history);
        g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);
        g_paste_history_flush (history);
        g_assert_cmpuint (stored_length (settings, "emptied-loading"), ==, 0);
    }

    g_autofree gchar *path = g_paste_util_get_history_file_path ("emptied-unreadable", "xml");

    g_assert_true (g_file_set_contents (path, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<history version=\"2.0\">\n"
                                              "  <item kind=\"Text\"><value><![CDATA[secret]]></value></item>\n", -1, NULL));

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load (history, "emptied-unreadable");
    g_assert_true (g_paste_history_is_unreadable (history));
    g_assert_true (g_paste_history_empty (history, NULL));
    g_paste_history_flush (history);
    g_assert_cmpuint (stored_length (settings, "emptied-unreadable"), ==, 0);
}

/* What is added while a load is out -- a copy, an AddText -- is put on top of
 * what the load read and stored, rather than replaced by it and recorded
 * nowhere: the uuid its caller was answered with would name nothing. */
static void
test_add_during_load_kept (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("stored one"));

        items = g_list_append (items, g_paste_text_item_new ("stored two"));
        g_paste_storage_backend_write_history (backend, "added-during-load", items);
    }

    gint held = hold_storage_lock ();
    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    GPasteItem *added = g_paste_text_item_new ("added during the load");
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (added));

    g_paste_history_load_async (history, "added-during-load");
    g_paste_history_add (history, added);
    g_close (held, NULL);
    wait_load (history);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 3);
    g_assert_nonnull (g_paste_history_get_by_uuid (history, uuid));
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (history, 0)), ==, "added during the load");
    g_paste_history_flush (history);
    g_assert_cmpuint (stored_length (settings, "added-during-load"), ==, 3);
}

/* A delete of an unreadable history that fails leaves it unreadable: recording
 * resumed then would write the next copy over the data it could not read. */
static void
test_failed_delete_keeps_unreadable (void)
{
    if (!g_test_subprocess ())
    {
        if (!geteuid ())
        {
            g_test_skip ("root removes files from a directory it cannot write");
            return;
        }

        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*Could not read the history back*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autofree gchar *dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *path = g_paste_util_get_history_file_path ("undeletable", "xml");
    const gchar *contents = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<history version=\"2.0\">\n";

    g_assert_true (g_paste_util_ensure_history_dir_exists ());
    g_assert_true (g_file_set_contents (path, contents, -1, NULL));

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    g_autoptr (GError) error = NULL;

    g_paste_history_load (history, "undeletable");
    g_assert_true (g_paste_history_is_unreadable (history));
    /* Held, as a daemon's first load holds it: taking it on the way would
     * make the history directory private again, undoing the chmod below. */
    g_assert_true (g_paste_storage_backend_try_lock ());
    g_assert_cmpint (g_chmod (dir, 0500), ==, 0);

    /* As in test_rekey_unlistable_images (): its mode back first. */
    gboolean deleted = g_paste_history_delete (history, NULL, &error);

    g_assert_cmpint (g_chmod (dir, 0700), ==, 0);
    g_assert_false (deleted);
    g_assert_nonnull (error);
    g_assert_true (g_paste_history_is_unreadable (history));

    g_paste_history_add (history, g_paste_text_item_new ("next copy"));
    g_paste_history_flush (history);

    g_autofree gchar *raw = NULL;

    g_assert_true (g_file_get_contents (path, &raw, NULL, NULL));
    g_assert_cmpstr (raw, ==, contents);
}

/* What a write renames into place, a history or an image, is on the disk by
 * then; and an image whose write fails leaves nothing at its name, so the next
 * save writes it again rather than skip a file it takes for complete. */
static void
test_synced_before_install (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*Failed to materialize image*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    const gchar *name = "synced-before-install";
    const guchar pixel[] = { 51, 61, 71, 255 };
    g_autoptr (GBytes) bytes = g_bytes_new (pixel, sizeof (pixel));
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (1, 1, GDK_MEMORY_R8G8B8A8, bytes, 4);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_image_item_new (texture));
    g_autofree gchar *image = g_paste_file_backend_image_path (name, g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (items->data)));
    g_autofree gchar *image_tmp = g_strconcat (image, ".tmp", NULL);
    const gchar * const image_sync[] = { ".png.tmp", NULL };

    g_paste_settings_set_images_support (settings, TRUE);

    failed_syncs = image_sync;
    g_paste_storage_backend_write_history (backend, name, items);
    failed_syncs = NULL;
    g_assert_false (g_file_test (image, G_FILE_TEST_EXISTS));
    g_assert_false (g_file_test (image_tmp, G_FILE_TEST_EXISTS));

    g_paste_storage_backend_write_history (backend, name, items);
    g_assert_true (g_file_test (image, G_FILE_TEST_EXISTS));
    g_assert_false (moved_unsynced);
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* As test_synced_before_install (), through the encryption converter, which
 * writes its final chunk as the stream closes: a history or an image synced
 * before then would be installed without it. */
static void
test_encrypted_synced_before_install (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "passphrase");
    const gchar *name = "encrypted-synced-before-install";
    const guchar pixel[] = { 52, 62, 72, 255 };
    g_autoptr (GBytes) bytes = g_bytes_new (pixel, sizeof (pixel));
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (1, 1, GDK_MEMORY_R8G8B8A8, bytes, 4);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_image_item_new (texture));
    g_autofree gchar *image = g_paste_file_backend_image_path (name, g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (items->data)));
    g_autofree gchar *encrypted_image = g_paste_file_backend_encrypted_path (image);

    g_paste_settings_set_images_support (settings, TRUE);

    moved_unsynced = FALSE;
    g_paste_storage_backend_write_history (backend, name, items);
    g_assert_true (g_file_test (encrypted_image, G_FILE_TEST_EXISTS));
    g_assert_false (moved_unsynced);
}
#endif

/* An alternative representation that decodes to nothing -- base64 a corrupt
 * file cut to nothing, an empty blob in a database -- is no representation:
 * the item is read without it, from its plain value, and written back so. */
static void
test_empty_alternative (gconstpointer data)
{
    GPasteStorage kind = GPOINTER_TO_UINT (data);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (kind, settings);
    const gchar *name = "empty-alternative";
    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (backend, name);

    if (kind == G_PASTE_STORAGE_FILE)
    {
        g_assert_true (g_paste_util_ensure_history_dir_exists ());
        g_assert_true (g_file_set_contents (path,
                                            "<history version=\"2.0\">"
                                            "<item kind=\"Text\"><value><![CDATA[plain]]></value>"
                                            "<value mime=\"text-html\"><![CDATA[!!!!]]></value></item>"
                                            "</history>",
                                            -1, NULL));
    }
#ifdef G_PASTE_ENABLE_SQLITE
    else
    {
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("plain"));
        g_autoptr (sqlite3) db = NULL;

        g_paste_item_add_special_value (items->data, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML, g_bytes_new_static ("<b>plain</b>", 12)));
        g_paste_storage_backend_write_history (backend, name, items);
        g_assert_cmpint (sqlite3_open (path, &db), ==, SQLITE_OK);
        g_assert_cmpint (sqlite3_exec (db, "UPDATE special_values SET data = X'';", NULL, NULL, NULL), ==, SQLITE_OK);
    }
#endif

    g_autolist (GPasteItem) items = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &items, &size));
    g_assert_cmpuint (g_list_length (items), ==, 1);
    g_assert_cmpstr (g_paste_item_get_value (items->data), ==, "plain");
    g_assert_null (g_paste_item_get_special_values (items->data));
    g_paste_storage_backend_write_history (backend, name, items);
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* An encrypted file history called @name holding an image, whose history file
 * and image side file are handed back through @history and @image. */
static void
write_encrypted_history (GPasteSettings *settings,
                         const gchar    *name,
                         const gchar    *passphrase,
                         gchar         **history,
                         gchar         **image)
{
    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, passphrase);
    const guchar pixel[] = { 100, 200, 50, 255 };
    g_autoptr (GBytes) bytes = g_bytes_new (pixel, sizeof (pixel));
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (1, 1, GDK_MEMORY_R8G8B8A8, bytes, 4);
    g_autolist (GPasteItem) items = NULL;

    items = g_list_append (items, g_paste_text_item_new ("text"));
    items = g_list_append (items, g_paste_image_item_new (texture));
    g_paste_settings_set_images_support (settings, TRUE);
    g_paste_storage_backend_write_history (backend, name, items);

    g_autofree gchar *plain = g_paste_file_backend_image_path (name, g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (items->next->data)));

    *history = g_paste_storage_backend_get_history_file_path (backend, name);
    *image = g_paste_file_backend_encrypted_path (plain);
    g_assert_true (g_file_test (*image, G_FILE_TEST_EXISTS));
}

static GBytes *
read_bytes (const gchar *path)
{
    gchar *contents = NULL;
    gsize length;

    g_assert_true (g_file_get_contents (path, &contents, &length, NULL));

    return g_bytes_new_take (contents, length);
}

/* Whether anything a re-key puts beside the files of @name is still there. */
static gboolean
has_rekey_leftovers (const gchar *name)
{
    g_autoptr (GFile) history_dir = g_paste_util_get_history_dir ();
    g_autofree gchar *images_path = g_paste_file_backend_images_dir (name);
    g_autoptr (GFile) images_dir = g_file_new_for_path (images_path);
    GFile *dirs[] = { history_dir, images_dir };

    for (guint i = 0; i < G_N_ELEMENTS (dirs); ++i)
    {
        g_autoptr (GError) error = NULL;
        g_auto (GStrv) files = g_paste_util_list_directory (dirs[i], G_FILE_ATTRIBUTE_STANDARD_NAME, &error);

        g_assert_no_error (error);
        for (GStrv file = files; *file; ++file)
        {
            if (strstr (*file, ".rekey"))
                return TRUE;
        }
    }

    return FALSE;
}

/* A re-key the filesystem stops part way puts back what it had changed, and
 * leaves nothing beside the files. */
static void
test_rekey_rollback (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*injected failure moving*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "rekey-rollback";
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_encrypted_history (settings, name, "old passphrase", &history, &image);

    g_autoptr (GBytes) original_history = read_bytes (history);
    g_autoptr (GBytes) original_image = read_bytes (image);
    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "old passphrase");
    /* The image goes in before the history: failing the history's install is
     * failing the re-key once the image has already moved. */
    const gchar * const history_install[] = { ".xmls.rekey", NULL };
    gboolean split = TRUE;

    failed_moves = history_install;
    g_assert_false (g_paste_storage_backend_rekey (backend, name, "new passphrase", &split));
    failed_moves = NULL;
    g_assert_false (split);

    g_autoptr (GBytes) kept_history = read_bytes (history);
    g_autoptr (GBytes) kept_image = read_bytes (image);

    g_assert_true (g_bytes_equal (kept_history, original_history));
    g_assert_true (g_bytes_equal (kept_image, original_image));
    g_assert_false (has_rekey_leftovers (name));
}

/* When putting an image back fails too, the failure says the history is split,
 * and the original the image could not get back stays beside it. The history
 * never left its path, so nothing has to move it back: refusing every move
 * back still finds it there. */
static void
test_rekey_split (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*Could not put back*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "rekey-split";
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_encrypted_history (settings, name, "old passphrase", &history, &image);

    g_autoptr (GBytes) original_history = read_bytes (history);
    g_autoptr (GBytes) original_image = read_bytes (image);
    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "old passphrase");
    const gchar * const no_way_back[] = { ".xmls.rekey", ".xmls.rekey-old", ".pngs.rekey-old", NULL };
    gboolean split = FALSE;

    failed_moves = no_way_back;
    g_assert_false (g_paste_storage_backend_rekey (backend, name, "new passphrase", &split));
    failed_moves = NULL;
    g_assert_true (split);

    g_autoptr (GBytes) kept_history = read_bytes (history);
    g_autoptr (GBytes) moved_image = read_bytes (image);
    g_autofree gchar *aside = g_strconcat (image, ".rekey-old", NULL);
    g_autoptr (GBytes) kept_image = read_bytes (aside);

    g_assert_true (g_bytes_equal (kept_history, original_history));
    g_assert_false (g_bytes_equal (moved_image, original_image));
    g_assert_true (g_bytes_equal (kept_image, original_image));

    /* Which is what the next start puts back, the history's replacement left
     * beside it saying there is something to. */
    g_paste_file_backend_settle_rekeys ();

    g_autoptr (GBytes) put_back = read_bytes (image);

    g_assert_true (g_bytes_equal (put_back, original_image));
    g_assert_false (has_rekey_leftovers (name));
}

/* A re-key that goes through leaves nothing beside the files, and every one
 * of them opens with the new passphrase, each replacement on the disk before it
 * was renamed into place and the renames once their directory is synced -- on
 * a filesystem without hard links too, the originals then copied aside. */
static void
test_rekey_success (gconstpointer data)
{
    gboolean without_links = GPOINTER_TO_INT (data);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = (without_links) ? "rekey-without-links" : "rekey-success";
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_encrypted_history (settings, name, "old passphrase", &history, &image);

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "old passphrase");
        g_autofree gchar *history_dir = g_path_get_dirname (history);
        g_autofree gchar *real_dir = realpath (history_dir, NULL);
        gboolean split = TRUE;

        moved_unsynced = FALSE;
        refused_links = without_links;
        g_clear_pointer (&synced, g_hash_table_unref);
        g_assert_true (g_paste_storage_backend_rekey (backend, name, "new passphrase", &split));
        refused_links = FALSE;
        g_assert_false (split);
        g_assert_false (moved_unsynced);
        /* The renames are on the disk once their directory is. */
        g_assert_nonnull (real_dir);
        g_assert_true (synced && g_hash_table_contains (synced, real_dir));
    }

    g_assert_false (has_rekey_leftovers (name));

    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "new passphrase");
    g_autolist (GPasteItem) items = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &items, &size));
    g_assert_cmpuint (g_list_length (items), ==, 2);
    g_assert_nonnull (g_paste_image_item_get_png_bytes (G_PASTE_IMAGE_ITEM (items->next->data)));
}

static void
write_file (const gchar *path,
            const gchar *contents)
{
    g_autofree gchar *dir = g_path_get_dirname (path);

    g_assert_cmpint (g_mkdir_with_parents (dir, 0700), ==, 0);
    g_assert_true (g_file_set_contents (path, contents, -1, NULL));
}

static void
assert_contents (const gchar *path,
                 const gchar *expected)
{
    g_autofree gchar *contents = NULL;

    g_assert_true (g_file_get_contents (path, &contents, NULL, NULL));
    g_assert_cmpstr (contents, ==, expected);
}

/* An image's side file carried on past its stream -- a sync tool appending a
 * second version, a shorter file written over a longer one -- is read as its
 * first stream, which the checksum the history holds vouches for: refused, the
 * image would be dropped by the read and its file deleted by the next save. A
 * history document carried on stays refused, being read as unreadable and so
 * never written over. */
static void
test_side_file_carried_on (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "side-file-carried-on";
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_encrypted_history (settings, name, "passphrase", &history, &image);

    g_autoptr (GBytes) side_file = read_bytes (image);
    g_autoptr (GByteArray) carried_on = g_byte_array_new ();

    g_byte_array_append (carried_on, g_bytes_get_data (side_file, NULL), g_bytes_get_size (side_file));
    g_byte_array_append (carried_on, (const guint8 *) "and then some", 13);
    g_assert_true (g_file_set_contents (image, (const gchar *) carried_on->data, carried_on->len, NULL));

    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "passphrase");

    {
        g_autolist (GPasteItem) items = NULL;
        gsize size;

        g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &items, &size));
        g_assert_cmpuint (g_list_length (items), ==, 2);
        g_assert_true (G_PASTE_IS_IMAGE_ITEM (items->next->data));
    }

    g_autoptr (GBytes) document = read_bytes (history);
    g_autoptr (GByteArray) document_carried_on = g_byte_array_new ();

    g_byte_array_append (document_carried_on, g_bytes_get_data (document, NULL), g_bytes_get_size (document));
    g_byte_array_append (document_carried_on, (const guint8 *) "and then some", 13);
    g_assert_true (g_file_set_contents (history, (const gchar *) document_carried_on->data, document_carried_on->len, NULL));

    g_autolist (GPasteItem) items = NULL;
    gsize size;

    g_assert_false (g_paste_storage_backend_read_history (backend, name, NULL, &items, &size));
}

/* What a process dying while a re-key installs its files leaves: the image
 * renamed over, its original under its second name, the history still in
 * place with a second name of its own and its replacement beside it. */
static void
write_interrupted_rekey (const gchar *name,
                         gchar      **history,
                         gchar      **image)
{
    g_autofree gchar *images = g_paste_file_backend_images_dir (name);

    *history = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_ENCRYPTED_FILE));
    *image = g_build_filename (images, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef.pngs", NULL);

    g_autofree gchar *history_aside = g_strconcat (*history, ".rekey-old", NULL);
    g_autofree gchar *history_replacement = g_strconcat (*history, ".rekey", NULL);
    g_autofree gchar *image_aside = g_strconcat (*image, ".rekey-old", NULL);

    write_file (*history, "old history");
    g_assert_cmpint (link (*history, history_aside), ==, 0);
    write_file (history_replacement, "new history");
    write_file (*image, "new image");
    write_file (image_aside, "old image");
}

/* A re-key that never reached its last rename is put back at the next start,
 * before anything reads the history or verifies a passphrase against it. */
static void
test_rekey_settled_back (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "rekey-settled-back";
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_interrupted_rekey (name, &history, &image);
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    /* A passphrase this process holds already, so that the answer comes before
     * any keyring is asked. */
    g_paste_storage_backend_set_passphrase ("passphrase");
    g_assert_false (g_paste_storage_decryption_needed (settings));
    g_paste_storage_backend_set_passphrase (NULL);

    assert_contents (history, "old history");
    assert_contents (image, "old image");
    g_assert_false (has_rekey_leftovers (name));
}

/* A re-key that made its last rename and died before dropping the originals
 * has them dropped at the next start, the migration's included. */
static void
test_rekey_settled_forward (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_interrupted_rekey (name, &history, &image);

    g_autofree gchar *history_replacement = g_strconcat (history, ".rekey", NULL);

    /* The history's replacement renamed over it, its original keeping its
     * second name. */
    g_assert_cmpint (g_rename (history_replacement, history), ==, 0);

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GError) error = NULL;

    prompt->dismiss = TRUE;
    g_assert_true (run_migration (settings, prompt, &error));
    g_assert_no_error (error);

    assert_contents (history, "new history");
    assert_contents (image, "new image");
    g_assert_false (has_rekey_leftovers (name));
}

/* Until it is settled, a history a re-key left half done is refused rather
 * than read without the images it can no longer decrypt. */
static void
test_rekey_unsettled_refused (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*did not finish*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "rekey-unsettled";
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_encrypted_history (settings, name, "passphrase", &history, &image);

    g_autofree gchar *history_replacement = g_strconcat (history, ".rekey", NULL);

    write_file (history_replacement, "new history");

    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "passphrase");
    g_autolist (GPasteItem) items = NULL;
    gsize size;

    g_assert_false (g_paste_storage_backend_read_history (backend, name, NULL, &items, &size));
}

/* Deleting a history takes what a re-key left beside it along -- its images'
 * leftovers too, a plain history of the same name keeping their directory. */
static void
test_rekey_leftovers_deleted (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    for (guint plain_twin = 0; plain_twin < 2; ++plain_twin)
    {
        const gchar *name = (plain_twin) ? "rekey-deleted-twin" : "rekey-deleted";
        g_autofree gchar *history = NULL;
        g_autofree gchar *image = NULL;
        g_autofree gchar *plain = g_paste_util_get_history_file_path (name, "xml");

        write_interrupted_rekey (name, &history, &image);
        if (plain_twin)
            write_file (plain, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<history version=\"2.0\">\n</history>\n");

        g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "passphrase");

        g_paste_storage_backend_delete_history (backend, name, NULL);
        g_assert_false (g_file_test (history, G_FILE_TEST_EXISTS));
        g_assert_false (has_rekey_leftovers (name));
    }
}

/* A history whose delete fails keeps what says its re-key did not finish:
 * without it, the next read would no longer refuse a history split between
 * two passphrases, and the next settle would drop its originals. */
static void
test_rekey_marker_kept_by_failed_delete (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "rekey-undeletable";
    g_autofree gchar *history = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_ENCRYPTED_FILE));
    g_autofree gchar *marker = g_strconcat (history, ".rekey", NULL);
    g_autofree gchar *inside = g_build_filename (history, "not empty", NULL);
    g_autoptr (GError) error = NULL;

    /* Something no unlink takes: a directory holding a file. */
    write_file (inside, "content");
    write_file (marker, "new history");

    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "passphrase");

    g_paste_storage_backend_delete_history (backend, name, &error);
    g_assert_nonnull (error);
    g_assert_true (g_file_test (marker, G_FILE_TEST_EXISTS));

    g_assert_cmpint (g_unlink (inside), ==, 0);
    g_assert_cmpint (g_rmdir (history), ==, 0);
    g_assert_cmpint (g_unlink (marker), ==, 0);
}

/* An encrypted file far larger than anything GPaste stores -- a sparse one, a
 * sync tool's placeholder -- is refused unread rather than allocated for, which
 * would abort the process hosting the daemon. */
static void
test_encrypted_read_oversized (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*larger than anything GPaste stores*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autofree gchar *history = g_paste_util_get_history_file_path ("oversized", g_paste_storage_get_extension (G_PASTE_STORAGE_ENCRYPTED_FILE));

    g_assert_true (g_paste_util_ensure_history_dir_exists ());

    {
        g_autoptr (GFile) file = g_file_new_for_path (history);
        g_autoptr (GFileOutputStream) stream = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL);

        g_assert_nonnull (stream);
        g_assert_true (g_seekable_truncate (G_SEEKABLE (stream), (goffset) G_GUINT64_CONSTANT (1) << 40, NULL, NULL));
        g_assert_true (g_output_stream_close (G_OUTPUT_STREAM (stream), NULL, NULL));
    }

    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "passphrase");
    g_autolist (GPasteItem) items = NULL;
    gsize size;

    g_assert_false (g_paste_storage_backend_read_history (backend, "oversized", NULL, &items, &size));
    g_assert_cmpint (g_unlink (history), ==, 0);
}

/* A history a re-key left unsettled is not written over either: the settle
 * would put its original back over the write, which would then have been
 * answered as held -- an emptying undone at the next start. */
static void
test_rekey_unsettled_unwritten (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*did not finish; not writing over it*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "rekey-unwritten";
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_encrypted_history (settings, name, "passphrase", &history, &image);

    g_autofree gchar *history_replacement = g_strconcat (history, ".rekey", NULL);
    g_autoptr (GBytes) before = read_bytes (history);

    write_file (history_replacement, "new history");

    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "passphrase");
    g_autoptr (GBytes) after = NULL;

    g_assert_false (g_paste_storage_backend_write_history (backend, name, NULL));
    after = read_bytes (history);
    g_assert_true (g_bytes_equal (before, after));
}

/* What a re-key left beside a history that is gone -- a delete cut short after
 * the history went, or a split history removed by hand -- is a history deleted:
 * the next start drops it rather than put its original back, and the name reads
 * as a new history from then on. */
static void
test_rekey_settled_without_history (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "rekey-without-history";
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_interrupted_rekey (name, &history, &image);
    g_assert_cmpint (g_unlink (history), ==, 0);

    g_paste_file_backend_settle_rekeys ();
    g_assert_false (g_file_test (history, G_FILE_TEST_EXISTS));
    g_assert_false (has_rekey_leftovers (name));

    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "passphrase");
    g_autolist (GPasteItem) items = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &items, &size));
}

/* A file that names no valid history is no history's to settle. */
static void
test_rekey_settles_valid_names (void)
{
    g_autoptr (GFile) history_dir = g_paste_util_get_history_dir ();
    g_autofree gchar *dir = g_file_get_path (history_dir);
    const gchar * const strays[] = { ".xmls", "..xmls", "...xmls.rekey" };

    for (guint i = 0; i < G_N_ELEMENTS (strays); ++i)
    {
        g_autofree gchar *path = g_build_filename (dir, strays[i], NULL);

        write_file (path, "stray");
    }

    g_paste_file_backend_settle_rekeys ();

    for (guint i = 0; i < G_N_ELEMENTS (strays); ++i)
    {
        g_autofree gchar *path = g_build_filename (dir, strays[i], NULL);

        g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));
    }
}

/* A re-key settles what an earlier one left behind before starting, rather
 * than fail on the second name it would give the history. */
static void
test_rekey_after_leftovers (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "rekey-after-leftovers";
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_encrypted_history (settings, name, "old passphrase", &history, &image);

    g_autofree gchar *history_aside = g_strconcat (history, ".rekey-old", NULL);

    write_file (history_aside, "an older history");

    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "old passphrase");
    gboolean split = TRUE;

    g_assert_true (g_paste_storage_backend_rekey (backend, name, "new passphrase", &split));
    g_assert_false (split);
    g_assert_false (has_rekey_leftovers (name));
}

/* A history whose images cannot be listed is not re-keyed, but that is no
 * split: nothing of it moved. */
static void
test_rekey_unlistable_images (void)
{
    if (!g_test_subprocess ())
    {
        if (!geteuid ())
        {
            g_test_skip ("root lists a directory of mode 0");
            return;
        }

        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*Could not list the images*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "rekey-unlistable";
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_encrypted_history (settings, name, "old passphrase", &history, &image);

    g_autofree gchar *images = g_paste_file_backend_images_dir (name);
    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "old passphrase");
    gboolean split = TRUE;

    g_assert_cmpint (g_chmod (images, 0), ==, 0);

    /* Given back its mode before anything is asserted, so that a failure does
     * not leave the test's directories to a cleanup that cannot enter them. */
    gboolean rekeyed = g_paste_storage_backend_rekey (backend, name, "new passphrase", &split);

    g_assert_cmpint (g_chmod (images, 0700), ==, 0);
    g_assert_false (rekeyed);
    g_assert_false (split);
}

/* A ".pngs" under a name this backend never writes is no entry's side file: a
 * re-key leaves it as it is rather than failing to decrypt it. */
static void
test_rekey_foreign_side_file (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "rekey-foreign";
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_encrypted_history (settings, name, "old passphrase", &history, &image);

    g_autofree gchar *images = g_paste_file_backend_images_dir (name);
    g_autofree gchar *foreign = g_build_filename (images, "Screenshot.pngs", NULL);
    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "old passphrase");
    gboolean split = TRUE;

    write_file (foreign, "not ours");
    g_assert_true (g_paste_storage_backend_rekey (backend, name, "new passphrase", &split));
    g_assert_false (split);
    assert_contents (foreign, "not ours");
}

static void
on_rekey_settled (GObject      *source G_GNUC_UNUSED,
                  GAsyncResult *result,
                  gpointer      user_data)
{
    g_set_object ((GAsyncResult **) user_data, result);
}

/* A change of passphrase reads each history it re-keys once: asked first
 * whether the passphrase in use opens it, every history would have a key
 * derived and its document decrypted twice. */
static void
test_rekey_cost (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "one");
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("active"));

        g_paste_storage_backend_write_history (backend, name, items);
        g_paste_storage_backend_write_history (backend, "other", items);
    }

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_paste_storage_backend_set_passphrase ("one");

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    prompt->passphrase = "two";
    g_atomic_int_set (&derivations, 0);
    g_paste_storage_rekey_async (G_PASTE_PROMPT (prompt), settings, on_rekey_settled, &result);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &result, NULL);
    g_assert_true (g_paste_storage_rekey_finish (result, &error));
    g_assert_no_error (error);
    /* Two checking the passphrase in use, then per history its document read
     * with the old key, and written and read back with the new one. */
    g_assert_cmpint (g_atomic_int_get (&derivations), ==, 2 + 2 * 3);
    g_paste_storage_backend_set_passphrase (NULL);
}

/* Nor while a storage worker of this process runs -- the previous daemon's
 * re-key in gnome-shell, which holds the lock in this very process: what is
 * left beside a history may be what it is in the middle of renaming. */
static void
test_rekey_settled_while_busy (void)
{
    const gchar *name = "rekey-settled-while-busy";
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_interrupted_rekey (name, &history, &image);
    g_assert_true (g_paste_storage_backend_try_lock ());

    ++g_paste_storage_workers;
    g_paste_file_backend_settle_rekeys ();
    --g_paste_storage_workers;
    g_assert_true (has_rekey_leftovers (name));

    g_paste_file_backend_settle_rekeys ();
    g_assert_false (has_rekey_leftovers (name));
}

/* Asked to unlock a history there is nothing to unlock in -- a plain backend,
 * or a passphrase already set -- the unlock completes at once and asks nothing:
 * whatever were typed would be accepted, with nothing encrypted to refute it,
 * and remembered in the keyring. */
static void
test_decryption_nothing_to_unlock (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);

    prompt->passphrase = "anything";

    for (guint i = 0; i < 2; ++i)
    {
        g_autoptr (GAsyncResult) result = NULL;
        g_autoptr (GError) error = NULL;

        g_paste_settings_set_storage_backend (settings, (i) ? G_PASTE_STORAGE_ENCRYPTED_FILE : G_PASTE_STORAGE_FILE);
        g_paste_storage_backend_set_passphrase ((i) ? "set" : NULL);
        g_paste_storage_decryption_async (G_PASTE_PROMPT (prompt), settings, on_migration_settled, &result);
        g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &result, NULL);
        g_assert_true (g_paste_storage_decryption_finish (result, &error));
        g_assert_no_error (error);
    }

    g_assert_cmpuint (prompt->passphrase_asks, ==, 0);
    g_assert_cmpstr (g_paste_storage_backend_get_passphrase (), ==, "set");
    g_paste_storage_backend_set_passphrase (NULL);
}

/* An encrypted store holding nothing to check a passphrase against -- only the
 * placeholder a first read leaves -- has it asked for twice, as a new one is:
 * whatever is typed keys every history written from then on. One holding a
 * history has it asked for once. */
static void
test_decryption_confirms_unverifiable (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "xmls");
    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_assert_true (g_paste_util_ensure_history_dir_exists ());
    g_assert_true (g_file_set_contents (path, "", 0, NULL));
    prompt->passphrase = "typed once";

    for (guint i = 0; i < 2; ++i)
    {
        g_autoptr (GAsyncResult) result = NULL;
        g_autoptr (GError) error = NULL;

        if (i)
        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "typed once");
            g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("kept"));

            g_paste_storage_backend_write_history (backend, name, items);
            g_paste_storage_backend_set_passphrase (NULL);
        }

        g_paste_storage_decryption_async (G_PASTE_PROMPT (prompt), settings, on_migration_settled, &result);
        g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &result, NULL);
        g_assert_true (g_paste_storage_decryption_finish (result, &error));
        g_assert_no_error (error);
        g_assert_cmpuint (prompt->passphrase_asks, ==, i + 1);
        g_assert_cmpint (prompt->confirmed, ==, !i);
    }

    g_paste_storage_backend_set_passphrase (NULL);
}

/* Only what a wrong passphrase is refused over makes a store verifiable: a
 * file that is no stream refutes nothing, nor does one cut short in its first
 * frame, nor an encrypted database emptied -- its key check stays, but an open
 * keys an empty one afresh for whatever it is given. Asked once there, a typo
 * would key every history written from then on. */
static void
test_verifiable_needs_refutable (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autofree gchar *source = g_paste_util_get_history_file_path ("source", "xmls");
    g_autofree gchar *foreign = g_paste_util_get_history_file_path ("foreign", "xmls");
    g_autofree gchar *truncated = g_paste_util_get_history_file_path ("truncated", "xmls");
    g_autoptr (GPasteStorageBackend) file_backend = g_paste_file_backend_new_encrypted (settings, "passphrase");
    g_autolist (GPasteItem) none = NULL;
    g_autofree gchar *stream = NULL;
    gsize stream_length;

    g_paste_storage_backend_write_history (file_backend, "source", none);
    g_assert_true (g_file_get_contents (source, &stream, &stream_length, NULL));
    g_assert_cmpint (g_unlink (source), ==, 0);

    /* The length of a first frame where a stream has it, under another magic. */
    g_autofree gchar *impostor = g_memdup2 (stream, stream_length);

    memcpy (impostor, "NOTGPST1", 8);
    g_assert_true (g_file_set_contents (foreign, impostor, stream_length, NULL));
    g_assert_true (g_file_set_contents (truncated, stream, stream_length - 1, NULL));
    g_assert_false (g_paste_storage_passphrase_verifiable (G_PASTE_STORAGE_ENCRYPTED_FILE, settings));

    g_paste_storage_backend_write_history (file_backend, "emptied", none);
    g_assert_true (g_paste_storage_passphrase_verifiable (G_PASTE_STORAGE_ENCRYPTED_FILE, settings));

#ifdef G_PASTE_ENABLE_SQLITE
    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, "passphrase");
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("kept"));

        g_paste_storage_backend_write_history (backend, "emptied", items);
        g_assert_true (g_paste_storage_passphrase_verifiable (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings));
        g_paste_storage_backend_clear_history (backend, "emptied", NULL);
    }
    g_assert_false (g_paste_storage_passphrase_verifiable (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings));
#endif

    g_paste_storage_backend_set_passphrase (NULL);
}

/* Nor is a passphrase asked for to change the one of a store this build cannot
 * open: it verifies none, so the unlock would take whatever is typed. */
static void
test_rekey_unopenable (void)
{
    if (!kind_hidable ())
        return;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    hidden_kind = G_PASTE_STORAGE_ENCRYPTED_FILE;
    prompt->passphrase = "anything";
    g_paste_storage_rekey_async (G_PASTE_PROMPT (prompt), settings, on_rekey_settled, &result);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &result, NULL);
    hidden_kind = -1;

    g_assert_false (g_paste_storage_rekey_finish (result, &error));
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    g_assert_cmpuint (prompt->passphrase_asks, ==, 0);
}

/* What a change of passphrase left is settled only while nothing else can be
 * writing: with another process holding the storage lock -- a daemon handing
 * over, perhaps still re-keying -- it is left for a start that can take it. */
static void
test_rekey_settled_under_lock (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    const gchar *name = "rekey-settled-under-lock";
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_interrupted_rekey (name, &history, &image);

    gint fd = hold_storage_lock ();

    g_paste_file_backend_settle_rekeys ();
    g_assert_true (has_rekey_leftovers (name));

    /* Nor is what the journal records. */
    g_autoptr (GPtrArray) journaled = g_ptr_array_new ();
    g_autofree gchar *journal = journal_path ();

    g_ptr_array_add (journaled, (gpointer) name);
    g_assert_true (journal_write (JOURNAL_REKEY, G_PASTE_STORAGE_ENCRYPTED_FILE, journaled, NULL));
    journal_settle (NULL);
    g_assert_true (g_file_test (journal, G_FILE_TEST_EXISTS));

    g_close (fd, NULL);
    g_paste_file_backend_settle_rekeys ();
    g_assert_false (has_rekey_leftovers (name));
    assert_contents (history, "old history");
}

/* A change of passphrase rewrites the store on a thread of its own, which a
 * host waits out before it ends or lets go of the storage lock. */
static void
test_rekey_busy (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_encrypted_history (settings, name, "old passphrase", &history, &image);
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_paste_storage_backend_set_passphrase ("old passphrase");

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    prompt->passphrase = "new passphrase";
    g_assert_false (g_paste_storage_is_busy ());
    g_paste_storage_rekey_async (G_PASTE_PROMPT (prompt), settings, on_rekey_settled, &result);
    /* The worker starts and reports in two turns of the main loop, so it is
     * seen running between them. */
    g_paste_test_bus_wait_until (busy_or_answered, &result, NULL);
    g_assert_true (g_paste_storage_is_busy ());
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &result, NULL);
    g_assert_false (g_paste_storage_is_busy ());
    g_assert_true (g_paste_storage_rekey_finish (result, &error));
    g_assert_no_error (error);
    g_paste_storage_backend_set_passphrase (NULL);
}

/* A change of passphrase a process did not live to finish between two
 * histories is told at the next start, with the ones on the new passphrase,
 * through the prompt -- once, and whether or not a passphrase is then asked
 * for. One that finished leaves nothing to tell. */
static void
test_rekey_cut_short (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*change of passphrase was cut short*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    g_autofree gchar *journal = journal_path ();
    g_autoptr (GPtrArray) journaled = g_ptr_array_new ();

    {
        g_autoptr (GPasteStorageBackend) one = g_paste_file_backend_new_encrypted (settings, "one");
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("kept"));

        g_paste_storage_backend_write_history (one, name, items);
        g_paste_storage_backend_write_history (one, "other", items);
    }

    g_ptr_array_add (journaled, "other");
    g_assert_true (journal_write (JOURNAL_REKEY, G_PASTE_STORAGE_ENCRYPTED_FILE, journaled, NULL));
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_paste_storage_backend_set_passphrase ("one");

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    g_assert_true (g_paste_storage_decryption_needed (settings));
    g_assert_false (g_file_test (journal, G_FILE_TEST_EXISTS));
    g_paste_storage_decryption_async (G_PASTE_PROMPT (prompt), settings, on_migration_settled, &result);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &result, NULL);
    g_assert_true (g_paste_storage_decryption_finish (result, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (prompt->reports, ==, 1);
    g_assert_nonnull (strstr (prompt->message, "“other” are on the new passphrase"));
    g_assert_cmpuint (prompt->passphrase_asks, ==, 0);
    g_assert_false (g_paste_storage_decryption_needed (settings));

    /* The second history's re-key begins with the first recorded. */
    g_clear_object (&result);
    prompt->passphrase = "two";
    rekey_journals = g_ptr_array_new_with_free_func (g_free);
    g_paste_storage_rekey_async (G_PASTE_PROMPT (prompt), settings, on_rekey_settled, &result);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &result, NULL);
    g_assert_true (g_paste_storage_rekey_finish (result, &error));
    g_assert_false (g_file_test (journal, G_FILE_TEST_EXISTS));
    g_assert_cmpuint (rekey_journals->len, ==, 2);
    g_assert_cmpstr (g_ptr_array_index (rekey_journals, 0), ==, "");
    g_assert_true (g_str_has_prefix (g_ptr_array_index (rekey_journals, 1), "rekey "));
    g_clear_pointer (&rekey_journals, g_ptr_array_unref);
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
    g_paste_storage_backend_set_passphrase (NULL);
}

/* A change of passphrase that fails in the process puts back what it did and
 * says so itself: it leaves no journal for the next start to report again. */
static void
test_rekey_failed_leaves_no_journal (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*injected failure moving*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    g_autofree gchar *journal = journal_path ();

    {
        g_autoptr (GPasteStorageBackend) one = g_paste_file_backend_new_encrypted (settings, "one");
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("kept"));

        g_paste_storage_backend_write_history (one, name, items);
        g_paste_storage_backend_write_history (one, "other", items);
    }

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_paste_storage_backend_set_passphrase ("one");

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    prompt->passphrase = "two";
    rekey_journals = g_ptr_array_new_with_free_func (g_free);
    failed_rekey = 2;
    g_paste_storage_rekey_async (G_PASTE_PROMPT (prompt), settings, on_rekey_settled, &result);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &result, NULL);
    failed_rekey = 0;
    failed_moves = NULL;

    g_assert_false (g_paste_storage_rekey_finish (result, &error));
    g_assert_true (g_str_has_prefix (g_ptr_array_index (rekey_journals, 1), "rekey "));
    g_assert_false (g_file_test (journal, G_FILE_TEST_EXISTS));
    g_clear_pointer (&rekey_journals, g_ptr_array_unref);
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
    g_paste_storage_backend_set_passphrase (NULL);
}

/* Histories split across two passphrases -- what a change whose rollback failed
 * leaves -- are opened by the one the active history takes, the others staying
 * closed on theirs, and a change of passphrase re-keys the ones it opens rather
 * than fail on the rest: refusing every passphrase, nothing would open again. */
static void
test_rekey_split_passphrases (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*is on another passphrase, which it keeps*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);

    {
        g_autoptr (GPasteStorageBackend) one = g_paste_file_backend_new_encrypted (settings, "one");
        g_autoptr (GPasteStorageBackend) two = g_paste_file_backend_new_encrypted (settings, "two");
        g_autolist (GPasteItem) active = g_list_append (NULL, g_paste_text_item_new ("active"));
        g_autolist (GPasteItem) other = g_list_append (NULL, g_paste_text_item_new ("other"));

        g_paste_storage_backend_write_history (one, name, active);
        g_paste_storage_backend_write_history (two, "other", other);
    }

    g_assert_true (g_paste_storage_passphrase_can_decrypt (G_PASTE_STORAGE_ENCRYPTED_FILE, settings, "one"));
    g_assert_false (g_paste_storage_passphrase_can_decrypt (G_PASTE_STORAGE_ENCRYPTED_FILE, settings, "two"));

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_paste_storage_backend_set_passphrase ("one");

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    prompt->passphrase = "three";
    g_paste_storage_rekey_async (G_PASTE_PROMPT (prompt), settings, on_rekey_settled, &result);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &result, NULL);
    g_assert_true (g_paste_storage_rekey_finish (result, &error));
    g_assert_no_error (error);

    g_autoptr (GPasteStorageBackend) three = g_paste_file_backend_new_encrypted (settings, "three");
    g_autoptr (GPasteStorageBackend) two = g_paste_file_backend_new_encrypted (settings, "two");
    g_autolist (GPasteItem) active = NULL;
    g_autolist (GPasteItem) other = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (three, name, NULL, &active, &size));
    g_assert_cmpuint (g_list_length (active), ==, 1);
    g_assert_true (g_paste_storage_backend_read_history (two, "other", NULL, &other, &size));
    g_assert_cmpuint (g_list_length (other), ==, 1);
    g_paste_storage_backend_set_passphrase (NULL);
}

/* What the user is told about a re-key that left the history split is that it
 * did, not that the history still takes the passphrase it had. */
static void
test_rekey_split_reported (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*left partly on the new passphrase*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_encrypted_history (settings, name, "old passphrase", &history, &image);
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_paste_storage_backend_set_passphrase ("old passphrase");

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;
    const gchar * const no_way_back[] = { ".xmls.rekey", ".pngs.rekey-old", NULL };

    prompt->passphrase = "new passphrase";
    failed_moves = no_way_back;
    g_paste_storage_rekey_async (G_PASTE_PROMPT (prompt), settings, on_rekey_settled, &result);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &result, NULL);
    failed_moves = NULL;

    g_assert_false (g_paste_storage_rekey_finish (result, &error));
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    g_assert_cmpuint (prompt->reports, ==, 1);
    g_assert_true (g_str_has_prefix (prompt->message, g_paste_prompt_text (G_PASTE_PROMPT_TEXT_REKEY_SPLIT_DESCRIPTION)));
}
#endif

static void
on_doctor_settled (GObject      *source G_GNUC_UNUSED,
                   GAsyncResult *result,
                   gpointer      user_data)
{
    g_set_object ((GAsyncResult **) user_data, result);
}

/* Check the store through @prompt, handing back its report. */
static GStrv
run_doctor (GPasteSettings      *settings,
            TestMigrationPrompt *prompt,
            gboolean            *healthy)
{
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;
    GStrv report = NULL;

    g_paste_storage_doctor_async (G_PASTE_PROMPT (prompt), settings, on_doctor_settled, &result);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &result, NULL);
    g_assert_true (g_paste_storage_doctor_finish (result, &report, healthy, &error));
    g_assert_no_error (error);
    g_assert_false (g_paste_storage_is_busy ());

    return report;
}

static guint
mode_of (const gchar *path)
{
    GStatBuf st;

    g_assert_cmpint (g_stat (path, &st), ==, 0);

    return st.st_mode & 0777;
}

/* A line of the report is one line, whatever the name it carries: a folder
 * named with line breaks -- anybody able to write into the history folder can
 * make one -- would otherwise add lines of its own choosing to the report,
 * which has one finding per line. Its control characters are shown as U+FFFD. */
static void
test_doctor_name_one_line (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autofree gchar *history_dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *forged = g_build_filename (history_dir, "x\nRestricted forged to its owner\ny", NULL);

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);
    g_assert_true (g_paste_storage_backend_try_lock ());
    g_assert_cmpint (g_mkdir_with_parents (forged, 0755), ==, 0);
    g_assert_cmpint (g_chmod (forged, 0755), ==, 0);

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    gboolean healthy = FALSE;
    g_auto (GStrv) report = run_doctor (settings, prompt, &healthy);
    gboolean named = FALSE;

    g_assert_true (healthy);
    for (GStrv line = report; *line; ++line)
    {
        g_assert_null (strchr (*line, '\n'));
        named |= strstr (*line, "x\xef\xbf\xbdRestricted forged to its owner\xef\xbf\xbdy") != NULL;
    }
    g_assert_true (named);
    g_assert_cmpint (g_rmdir (forged), ==, 0);
}

/* The history folder and every folder in it are restricted to their owner, and
 * said to be once; a second check finds nothing to say. */
static void
test_doctor_permissions (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autofree gchar *history_dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *images_root = g_build_filename (history_dir, "images", NULL);
    g_autofree gchar *images = g_paste_file_backend_images_dir ("doctor");

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);
    /* Held, as the daemon running the check holds it: taking it on the way would
     * make the history directory private again, undoing the chmod below. */
    g_assert_true (g_paste_storage_backend_try_lock ());
    g_assert_cmpint (g_mkdir_with_parents (images, 0755), ==, 0);
    /* Every one of the three, whatever the umask made them. */
    g_assert_cmpint (g_chmod (history_dir, 0755), ==, 0);
    g_assert_cmpint (g_chmod (images_root, 0755), ==, 0);
    g_assert_cmpint (g_chmod (images, 0755), ==, 0);

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    gboolean healthy = FALSE;
    g_auto (GStrv) report = run_doctor (settings, prompt, &healthy);

    g_assert_true (healthy);
    g_assert_cmpuint (g_strv_length (report), ==, 3);
    g_assert_cmpuint (mode_of (history_dir), ==, 0700);
    g_assert_cmpuint (mode_of (images_root), ==, 0700);
    g_assert_cmpuint (mode_of (images), ==, 0700);
    g_assert_cmpuint (prompt->passphrase_asks, ==, 0);

    g_auto (GStrv) again = run_doctor (settings, prompt, &healthy);

    g_assert_true (healthy);
    g_assert_cmpstrv (again, (const gchar * const []) { NULL });
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* What a change of passphrase that could not be undone left -- the split it
 * sends the user to the check for -- is settled by the check, as at a start,
 * rather than reported as a history that cannot be read. */
static void
test_doctor_settles_rekey (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*Failed to install the re-encrypted*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "doctor-split";
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_encrypted_history (settings, name, "old passphrase", &history, &image);

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "old passphrase");
        const gchar * const no_way_back[] = { ".xmls.rekey", ".xmls.rekey-old", ".pngs.rekey-old", NULL };
        gboolean split = FALSE;

        failed_moves = no_way_back;
        g_assert_false (g_paste_storage_backend_rekey (backend, name, "new passphrase", &split));
        failed_moves = NULL;
        g_assert_true (split);
    }

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_paste_storage_backend_set_passphrase ("old passphrase");

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    gboolean healthy = FALSE;
    g_auto (GStrv) report = run_doctor (settings, prompt, &healthy);

    g_paste_storage_backend_set_passphrase (NULL);
    for (GStrv line = report; *line; ++line)
        g_assert_null (strstr (*line, name));
    g_assert_true (healthy);
    g_assert_false (has_rekey_leftovers (name));
}
#endif

#if defined(G_PASTE_ENABLE_ENCRYPTION) && defined(G_PASTE_ENABLE_SQLITE)
/* The check reads every row a history stores, past max-history-size: damage
 * in a row the load leaves unread is damage all the same, and what the check
 * is there to report. */
static void
test_doctor_reads_whole (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*failed to decrypt an item*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "doctor-whole";
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_ENCRYPTED_SQLITE));
    g_autolist (GPasteItem) items = NULL;
    sqlite3 *db = NULL;

    for (guint i = 0; i < 6; ++i)
    {
        g_autofree gchar *value = g_strdup_printf ("item %u", i);

        items = g_list_append (items, g_paste_text_item_new (value));
    }

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings, "passphrase");

        g_assert_true (g_paste_storage_backend_write_history (backend, name, items));
    }

    /* The back row, past the 5 a load reads. */
    g_assert_cmpint (sqlite3_open (path, &db), ==, SQLITE_OK);
    g_assert_cmpint (sqlite3_exec (db, "UPDATE items SET value = X'00' WHERE rank = (SELECT MIN (rank) FROM items);", NULL, NULL, NULL), ==, SQLITE_OK);
    sqlite3_close (db);
    g_paste_settings_set_max_history_size (settings, 5);
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_SQLITE);
    g_paste_storage_backend_set_passphrase ("passphrase");

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    gboolean healthy = TRUE;
    g_auto (GStrv) report = run_doctor (settings, prompt, &healthy);
    gboolean named = FALSE;

    g_assert_false (healthy);
    for (GStrv line = report; *line; ++line)
        named |= strstr (*line, name) != NULL;
    g_assert_true (named);
    g_paste_storage_backend_set_passphrase (NULL);
}
#endif

static void
on_other_passphrase (GObject      *source G_GNUC_UNUSED,
                     GAsyncResult *result,
                     gpointer      user_data)
{
    g_set_object ((GAsyncResult **) user_data, result);
}

/* What a prompt is handed is UTF-8, whatever history names it carries: a
 * file's name need not be, the Adwaita dialog shows nothing of a description
 * that is not, and the in-shell one throws on it, the user then told nothing
 * or asked for nothing. */
static void
test_prompt_texts_valid (void)
{
    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GAsyncResult) result = NULL;

    g_paste_prompt_report (G_PASTE_PROMPT (prompt), "Could not read \xff", "The history \xe9 could not be read");
    g_assert_true (g_utf8_validate (prompt->title, -1, NULL));
    g_assert_true (g_utf8_validate (prompt->message, -1, NULL));

    prompt->passphrase = "other";
    g_paste_prompt_other_passphrase_async (G_PASTE_PROMPT (prompt), "The histories \xe9 take another passphrase",
                                           "Wrong for \xff", on_other_passphrase, &result);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &result, NULL);
    g_assert_true (g_utf8_validate (prompt->passphrase_description, -1, NULL));
    g_assert_true (g_utf8_validate (prompt->passphrase_error, -1, NULL));
}

/* A folder in the history directory swapped for a link once the check has
 * looked at it is not followed: what the link points at is not GPaste's to
 * restrict, nor to walk. */
static void
test_doctor_link_swapped_in (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autofree gchar *history_dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *entry = g_build_filename (history_dir, "swapped-for-link", NULL);
    g_autofree gchar *outside = g_build_filename (g_get_user_cache_dir (), "not-gpaste's", NULL);

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);
    g_assert_true (g_paste_storage_backend_try_lock ());
    /* Open to others, as the folder's would be once swapped: a look at it
     * restricts it, and so whatever stands in its place by then. */
    g_assert_cmpint (g_mkdir_with_parents (entry, 0755), ==, 0);
    g_assert_cmpint (g_chmod (entry, 0755), ==, 0);
    g_assert_cmpint (g_mkdir_with_parents (outside, 0755), ==, 0);
    g_assert_cmpint (g_chmod (outside, 0755), ==, 0);
    swap_dir = history_dir;
    swap_target = outside;

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    gboolean healthy = FALSE;
    g_auto (GStrv) report = run_doctor (settings, prompt, &healthy);

    g_assert_null (swap_dir);
    g_assert_cmpuint (mode_of (outside), ==, 0755);
    for (GStrv line = report; *line; ++line)
        g_assert_null (strstr (*line, "not-gpaste's"));
    g_assert_cmpint (g_unlink (entry), ==, 0);
    g_assert_cmpint (g_rmdir (outside), ==, 0);
}

/* A history folder moved elsewhere and linked back is the one checked: the
 * folder it links to is restricted as the folder itself would be. */
static void
test_doctor_linked_folder (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autofree gchar *history_dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *moved = g_strconcat (history_dir, "-moved", NULL);

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);
    g_assert_true (g_paste_util_ensure_history_dir_exists ());
    g_assert_cmpint (g_rename (history_dir, moved), ==, 0);
    g_assert_cmpint (symlink (moved, history_dir), ==, 0);
    /* Held, as in /storage/doctor/permissions: taking it on the way would make
     * the folder private again through the link, undoing the chmod below. */
    g_assert_true (g_paste_storage_backend_try_lock ());
    g_assert_cmpint (g_chmod (moved, 0755), ==, 0);

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    gboolean healthy = FALSE;
    g_auto (GStrv) report = run_doctor (settings, prompt, &healthy);
    guint mode = mode_of (moved);

    g_assert_cmpint (g_unlink (history_dir), ==, 0);
    g_assert_cmpint (g_rename (moved, history_dir), ==, 0);
    g_assert_true (healthy);
    g_assert_cmpuint (mode, ==, 0700);
    g_assert_cmpuint (g_strv_length (report), ==, 1);
}

/* A history that cannot be read is said to be, and leaves the store unhealthy:
 * nothing can put it right. The report is UTF-8 whatever the names of the
 * files it tells of. */
static void
test_doctor_unreadable (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*Failed to parse history file*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("fine"));
        g_autofree gchar *broken = g_paste_storage_backend_get_history_file_path (backend, "broken");

        g_autofree gchar *misnamed = g_paste_storage_backend_get_history_file_path (backend, "\xff\xfe");

        g_paste_storage_backend_write_history (backend, "fine", items);
        g_assert_true (g_file_set_contents (broken, "<history version=\"2.0\"><item", -1, NULL));
        g_assert_true (g_file_set_contents (misnamed, "<history version=\"2.0\"><item", -1, NULL));
    }

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    gboolean healthy = TRUE;
    g_auto (GStrv) report = run_doctor (settings, prompt, &healthy);

    g_assert_false (healthy);
    g_assert_cmpuint (g_strv_length (report), ==, 2);

    guint broken_lines = 0;

    for (GStrv line = report; *line; ++line)
    {
        g_assert_true (g_utf8_validate (*line, -1, NULL));
        broken_lines += strstr (*line, "“broken”") != NULL;
    }
    g_assert_cmpuint (broken_lines, ==, 1);
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
static void
write_under (GPasteSettings *settings,
             const gchar    *passphrase,
             const gchar    *name)
{
    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, passphrase);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new (name));

    g_paste_storage_backend_write_history (backend, name, items);
}

static gboolean
opens_under (GPasteSettings *settings,
             const gchar    *passphrase,
             const gchar    *name)
{
    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, passphrase);
    g_autolist (GPasteItem) items = NULL;
    gsize size = 0;

    return g_paste_storage_backend_read_history (backend, name, NULL, &items, &size) && g_list_length (items) == 1;
}

/* Histories left on another passphrase are encrypted with the one the history
 * in use takes once the user types theirs, which is asked for naming them and
 * never offered to be remembered; one opening none of them is asked again,
 * saying so. */
static void
test_doctor_split_repaired (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);

    write_under (settings, "one", name);
    write_under (settings, "two", "other");
    write_under (settings, "two", "third");
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_paste_storage_backend_set_passphrase ("one");

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    const gchar * const typed[] = { "wrong", "two", NULL };
    gboolean healthy = FALSE;

    prompt->passphrases = typed;

    g_auto (GStrv) report = run_doctor (settings, prompt, &healthy);

    g_assert_true (healthy);
    g_assert_cmpuint (g_strv_length (report), ==, 2);
    g_assert_cmpuint (prompt->passphrase_asks, ==, 2);
    g_assert_cmpstr (prompt->passphrase_error, ==, g_paste_prompt_text (G_PASTE_PROMPT_TEXT_OTHER_PASSPHRASE_WRONG));
    g_assert_nonnull (strstr (prompt->passphrase_description, "other"));
    g_assert_nonnull (strstr (prompt->passphrase_description, "third"));
    g_assert_false (prompt->offered_remember);
    g_assert_true (opens_under (settings, "one", name));
    g_assert_true (opens_under (settings, "one", "other"));
    g_assert_true (opens_under (settings, "one", "third"));
    g_assert_cmpstr (g_paste_storage_backend_get_passphrase (), ==, "one");
    g_paste_storage_backend_set_passphrase (NULL);
}

/* Dismissed, the passphrase asked for leaves those histories as they are and
 * the store unhealthy, each of them said to be still on another passphrase. */
static void
test_doctor_split_dismissed (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);

    write_under (settings, "one", name);
    write_under (settings, "two", "other");
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_paste_storage_backend_set_passphrase ("one");

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    gboolean healthy = TRUE;
    g_auto (GStrv) report = run_doctor (settings, prompt, &healthy);

    g_assert_false (healthy);
    g_assert_cmpuint (prompt->passphrase_asks, ==, 1);
    g_assert_cmpuint (g_strv_length (report), ==, 1);
    g_assert_nonnull (strstr (report[0], "“other”"));
    g_assert_true (opens_under (settings, "two", "other"));
    g_assert_true (opens_under (settings, "one", name));
    g_paste_storage_backend_set_passphrase (NULL);
}

/* A locked history is unlocked first; left locked, what it holds is not
 * checked and the store is not called healthy. */
static void
test_doctor_locked (void)
{
    /* The keyring is looked up first, which the test bus has none of. */
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*Could not look up the history passphrase in the keyring*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);

    write_under (settings, "one", name);
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    gboolean healthy = TRUE;
    g_auto (GStrv) report = run_doctor (settings, prompt, &healthy);

    g_assert_false (healthy);
    g_assert_cmpuint (prompt->passphrase_asks, ==, 1);
    g_assert_null (prompt->passphrase_description);
    g_assert_cmpuint (g_strv_length (report), ==, 1);
    g_assert_null (g_paste_storage_backend_get_passphrase ());
}
#endif

int
main (int    argc,
      char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

    g_test_add_func ("/storage/failed-write-images", test_failed_write_keeps_images);
    g_test_add_func ("/storage/synced-before-install", test_synced_before_install);
    g_test_add_func ("/storage/lock/file-unusable", test_lock_file_unusable);
    g_test_add_func ("/storage/lock/dir-unmade", test_lock_dir_unmade);
    g_test_add_func ("/storage/doctor/permissions", test_doctor_permissions);
    g_test_add_func ("/storage/doctor/name-one-line", test_doctor_name_one_line);
    g_test_add_func ("/storage/doctor/linked-folder", test_doctor_linked_folder);
    g_test_add_func ("/storage/doctor/link-swapped-in", test_doctor_link_swapped_in);
    g_test_add_func ("/storage/prompt/texts-valid", test_prompt_texts_valid);
#if defined(G_PASTE_ENABLE_ENCRYPTION) && defined(G_PASTE_ENABLE_SQLITE)
    g_test_add_func ("/storage/doctor/reads-whole", test_doctor_reads_whole);
#endif
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_test_add_func ("/storage/doctor/settles-rekey", test_doctor_settles_rekey);
#endif
    g_test_add_func ("/storage/doctor/unreadable", test_doctor_unreadable);
    g_test_add_func ("/storage/lock/two-waits", test_storage_lock_two_waits);
    g_test_add_func ("/storage/lock/taken-is-ours", test_taken_lock_is_ours);
    g_test_add_func ("/storage/lock/load-after-release", test_load_after_release_reads_nothing);
    g_test_add_func ("/storage/lock/flushed-history", test_flushed_history_loads_nothing);
    g_test_add_func ("/storage/lock/flushed-switch-supersedes-load", test_flushed_switch_supersedes_load);
    g_test_add_func ("/storage/switch-writes-nothing", test_switch_writes_nothing);
    g_test_add_func ("/storage/lock/dropped-history", test_dropped_history_ends_its_load);
    g_test_add_func ("/storage/lock/store-methods", test_store_methods_wait_for_lock);
    g_test_add_func ("/storage/history/empty-reaches-the-store", test_empty_reaches_the_store);
    g_test_add_func ("/storage/history/failed-delete-keeps-unreadable", test_failed_delete_keeps_unreadable);
    g_test_add_func ("/storage/history/add-during-load-kept", test_add_during_load_kept);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_test_add_func ("/storage/encrypted-stream-bounds", test_encrypted_stream_bounds);
#endif
    g_test_add_func ("/storage/image-name-not-utf8", test_image_name_not_utf8);
    g_test_add_func ("/storage/image-write-failed", test_image_write_failed);
    g_test_add_func ("/storage/new-history-write-failed", test_new_history_write_failed);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_test_add_func ("/storage/encrypted-synced-before-install", test_encrypted_synced_before_install);
#endif
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_test_add_func ("/storage/passphrase-survives-damage", test_passphrase_survives_damage);
    g_test_add_func ("/storage/passphrase/verify-cost", test_passphrase_verify_cost);
    g_test_add_func ("/storage/passphrase-survives-unreadable", test_passphrase_survives_unreadable);
    g_test_add_func ("/storage/new-history-name-fits", test_new_history_name_fits);
    g_test_add_data_func ("/storage/secure-memory-wiped/file", GUINT_TO_POINTER (G_PASTE_STORAGE_ENCRYPTED_FILE), test_secure_memory_wiped);
    g_test_add_func ("/storage/secure-memory-wiped/halfway", test_secure_memory_wiped_halfway);
#ifdef G_PASTE_ENABLE_SQLITE
    g_test_add_data_func ("/storage/secure-memory-wiped/sqlite", GUINT_TO_POINTER (G_PASTE_STORAGE_ENCRYPTED_SQLITE), test_secure_memory_wiped);
#endif
#endif
    g_test_add_data_func ("/storage/empty-alternative/file", GUINT_TO_POINTER (G_PASTE_STORAGE_FILE), test_empty_alternative);
#ifdef G_PASTE_ENABLE_SQLITE
    g_test_add_data_func ("/storage/empty-alternative/sqlite", GUINT_TO_POINTER (G_PASTE_STORAGE_SQLITE), test_empty_alternative);
#endif
    g_test_add_data_func ("/storage/migration/encrypted-file-identity", GUINT_TO_POINTER (G_PASTE_STORAGE_ENCRYPTED_FILE), test_migration_keeps_unopenable_identity);
    g_test_add_data_func ("/storage/migration/sqlite-identity", GUINT_TO_POINTER (G_PASTE_STORAGE_SQLITE), test_migration_keeps_unopenable_identity);
    g_test_add_data_func ("/storage/migration/encrypted-sqlite-identity", GUINT_TO_POINTER (G_PASTE_STORAGE_ENCRYPTED_SQLITE), test_migration_keeps_unopenable_identity);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_test_add_func ("/storage/unlock/unopenable-flavour", test_unlock_skips_unopenable_flavour);
    g_test_add_func ("/storage/rekey/rollback", test_rekey_rollback);
    g_test_add_func ("/storage/side-file-carried-on", test_side_file_carried_on);
    g_test_add_func ("/storage/rekey/split", test_rekey_split);
    g_test_add_data_func ("/storage/rekey/success", GINT_TO_POINTER (FALSE), test_rekey_success);
    g_test_add_data_func ("/storage/rekey/without-hard-links", GINT_TO_POINTER (TRUE), test_rekey_success);
    g_test_add_func ("/storage/rekey/split-reported", test_rekey_split_reported);
    g_test_add_func ("/storage/rekey/settled-back", test_rekey_settled_back);
    g_test_add_func ("/storage/rekey/settled-forward", test_rekey_settled_forward);
    g_test_add_func ("/storage/rekey/unsettled-refused", test_rekey_unsettled_refused);
    g_test_add_func ("/storage/rekey/leftovers-deleted", test_rekey_leftovers_deleted);
    g_test_add_func ("/storage/rekey/marker-kept-by-failed-delete", test_rekey_marker_kept_by_failed_delete);
    g_test_add_func ("/storage/rekey/unsettled-unwritten", test_rekey_unsettled_unwritten);
    g_test_add_func ("/storage/encrypted-read-oversized", test_encrypted_read_oversized);
    g_test_add_func ("/storage/rekey/settled-without-history", test_rekey_settled_without_history);
    g_test_add_func ("/storage/rekey/split-passphrases", test_rekey_split_passphrases);
    g_test_add_func ("/storage/rekey/cost", test_rekey_cost);
    g_test_add_func ("/storage/rekey/cut-short", test_rekey_cut_short);
    g_test_add_func ("/storage/rekey/failed-leaves-no-journal", test_rekey_failed_leaves_no_journal);
    g_test_add_func ("/storage/doctor/split-repaired", test_doctor_split_repaired);
    g_test_add_func ("/storage/doctor/split-dismissed", test_doctor_split_dismissed);
    g_test_add_func ("/storage/doctor/locked", test_doctor_locked);
    g_test_add_func ("/storage/rekey/settled-under-lock", test_rekey_settled_under_lock);
    g_test_add_func ("/storage/rekey/settled-while-busy", test_rekey_settled_while_busy);
    g_test_add_func ("/storage/rekey/busy", test_rekey_busy);
    g_test_add_func ("/storage/decryption/nothing-to-unlock", test_decryption_nothing_to_unlock);
    g_test_add_func ("/storage/decryption/confirms-unverifiable", test_decryption_confirms_unverifiable);
    g_test_add_func ("/storage/decryption/verifiable-needs-refutable", test_verifiable_needs_refutable);
    g_test_add_func ("/storage/rekey/unopenable", test_rekey_unopenable);
    g_test_add_func ("/storage/rekey/settles-valid-names", test_rekey_settles_valid_names);
    g_test_add_func ("/storage/rekey/after-leftovers", test_rekey_after_leftovers);
    g_test_add_func ("/storage/rekey/unlistable-images", test_rekey_unlistable_images);
    g_test_add_func ("/storage/rekey/foreign-side-file", test_rekey_foreign_side_file);
    g_test_add_func ("/storage/migration/import-failure-cleartext", test_import_failure_leaves_no_cleartext);
    g_test_add_func ("/storage/migration/destination-passphrase", test_migration_destination_passphrase);
    g_test_add_func ("/storage/encrypted-read-leaves-no-copy", test_encrypted_read_leaves_no_copy);
#endif
#ifdef G_PASTE_ENABLE_SQLITE
    g_test_add_data_func ("/storage/sqlite/schema/alter", "ALTER TABLE items ADD COLUMN timeout", test_schema_failure);
    g_test_add_data_func ("/storage/sqlite/schema/version", "PRAGMA user_version = 3", test_schema_failure);
    g_test_add_data_func ("/storage/sqlite/schema/commit", "COMMIT;", test_schema_failure);
    g_test_add_func ("/storage/sqlite/checksum-refused", test_sqlite_checksum_refused);
    g_test_add_func ("/storage/sqlite/schema/unreadable-version", test_schema_version_unreadable);
    g_test_add_func ("/storage/sqlite/schema/upgraded-meanwhile", test_schema_upgraded_meanwhile);
    g_test_add_func ("/storage/sqlite/eviction-checkpoints-nothing", test_sqlite_eviction_checkpoints_nothing);
#endif
#if defined(G_PASTE_ENABLE_ENCRYPTION) && defined(G_PASTE_ENABLE_SQLITE)
    g_test_add_func ("/storage/sqlite/incomplete-crypto", test_incomplete_crypto);
    g_test_add_func ("/storage/sqlite/uncounted-crypto", test_uncounted_crypto);
    g_test_add_func ("/storage/sqlite/busy-verification", test_verification_waits_for_busy_database);
    g_test_add_func ("/storage/sqlite/verification-empty-active-database", test_verification_empty_active_database);
#endif
#ifdef G_PASTE_ENABLE_SQLITE
    g_test_add_func ("/storage/migration/unopenable-source", test_migration_clamps_unopenable_source);
    g_test_add_func ("/storage/migration/unoffered-choice", test_migration_refuses_unoffered_choice);
    g_test_add_func ("/storage/migration/import-failure", test_import_failure_leaves_no_copy);
    g_test_add_func ("/storage/migration/import-readability", test_import_needs_readable_copy);
    g_test_add_func ("/storage/migration/import-copies-whole", test_import_copies_whole);
    g_test_add_func ("/storage/migration/import-cut-short", test_import_cut_short);
    g_test_add_func ("/storage/migration/import-cut-short-then-migrate", test_import_cut_short_then_migrate);
    g_test_add_func ("/storage/migration/import-busy", test_import_busy);
    g_test_add_func ("/storage/migration/import-spares-unreadable-destination", test_import_spares_unreadable_destination);
    g_test_add_func ("/storage/migration/import-spares-unwritten-destination", test_import_spares_unwritten_destination);
    g_test_add_func ("/storage/sqlite/rank-compaction", test_sqlite_rank_compaction);
    g_test_add_func ("/storage/sqlite/diverged-write", test_sqlite_diverged_write);
    g_test_add_func ("/storage/sqlite/failed-write-leaves-no-database", test_sqlite_failed_write_leaves_no_database);
    g_test_add_func ("/storage/sqlite/reconcile-uncounted", test_sqlite_reconcile_uncounted);
    g_test_add_func ("/storage/sqlite/reconcile-failed", test_sqlite_reconcile_failed);
    g_test_add_func ("/storage/sqlite/diverged-history", test_sqlite_diverged_history);
    g_test_add_func ("/storage/sqlite/failed-write-answered", test_sqlite_failed_write_answered);
    for (gsize i = 0; i < G_N_ELEMENTS (incremental_failures); ++i)
    {
        g_autofree gchar *path = g_strdup_printf ("/storage/sqlite/incremental-failed/%s-%" G_GSIZE_FORMAT, incremental_failures[i].operation, i);

        g_test_add_data_func (path, &incremental_failures[i], test_sqlite_incremental_failed);
    }
    g_test_add_func ("/storage/sqlite/unranked-add", test_sqlite_unranked_add);
    g_test_add_func ("/storage/sqlite/item-too-large", test_sqlite_item_too_large);
#endif

    return g_paste_test_env_run ();
}
