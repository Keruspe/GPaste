// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include "gpaste-test-history.h"

#include <gpaste-test-bus.h>
#include <gpaste-test-env.h>
#include <gpaste-daemon/gpaste-daemon-methods.h>
#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-file-backend.h>
#include <gpaste-daemon/gpaste-history-private.h>
#include <gpaste-daemon/gpaste-history.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-storage-migration.h>
#include <gpaste-daemon/gpaste-text-item.h>

#include <glib/gstdio.h>
#include <sys/stat.h>

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
/* Every derivation failing while set, as one Argon2id's memory is refused
 * for -- a memory limit, strict overcommit -- fails. */
static gboolean refuse_derivations;
/* Run once, from inside the next derivation: what another connection does
 * while this one derives. */
static void (*during_derivation) (void);

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
    if (refuse_derivations)
        return -1;
    if (during_derivation)
    {
        void (*run) (void) = during_derivation;

        during_derivation = NULL;
        run ();
    }

    return original (out, outlen, passwd, passwdlen, salt, crypto_pwhash_OPSLIMIT_MIN, crypto_pwhash_MEMLIMIT_MIN, alg);
}

/* How many times the process passphrase was read: a backend keyed with one of
 * its own, which is what a storage worker builds, reads none. */
static guint process_passphrase_reads;

const gchar *
g_paste_storage_backend_get_passphrase (void)
{
    typedef const gchar *(*Get) (void);
    Get original = (Get) dlsym (RTLD_NEXT, "g_paste_storage_backend_get_passphrase");

    g_assert_nonnull (original);
    g_atomic_int_inc (&process_passphrase_reads);

    return original ();
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

/* What GLib frees knowing its size -- g_free_sized (), g_slice_free (), the
 * data a GBytes holds inline -- goes to these where the C library has them,
 * searched as free () searches and then freed by it, which takes whatever
 * either takes. glibc's own call free () through its PLT, where the search
 * would see the block anyway; a C library implementing them otherwise would
 * not, and none this suite runs on does, which is why no case tells these
 * apart from free () alone. They are kept for such a library all the same:
 * without them its sized frees would go unsearched, the no-copy cases passing
 * over them, and /storage/secret-search-counts would fail there until they
 * were added back. */
void
free_sized (void  *memory,
            size_t size G_GNUC_UNUSED)
{
    free (memory);
}

void
free_aligned_sized (void  *memory,
                    size_t alignment G_GNUC_UNUSED,
                    size_t size      G_GNUC_UNUSED)
{
    free (memory);
}

/* A block realloc () moves is released holding what it held, which free ()
 * never sees. Whether it moves is the heap's call, so while @freed_secret is
 * set every one does, and one holding half the secret or more -- what a growing
 * buffer had decoded of it so far -- is counted as a copy left behind too. */
void *
realloc (void  *memory,
         size_t size)
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

/* Where a stand-in holds the threads reaching it, from gate_close () until
 * gate_open (), @held counting those it held: a storage worker kept waiting as
 * long as a case needs. The thread that closed it goes through, being the one
 * to open it again; and a thread is held ten seconds at most, failing the case
 * past that rather than leaving the binary to meson's timeout. A static Gate
 * needs no initialisation. */
typedef struct
{
    GMutex   mutex;
    GCond    cond;
    GThread *spared;
    gboolean closed;
    guint    held;
} Gate;

static void
gate_close (Gate *gate)
{
    g_mutex_lock (&gate->mutex);
    gate->spared = g_thread_self ();
    gate->closed = TRUE;
    g_atomic_int_set (&gate->held, 0);
    g_mutex_unlock (&gate->mutex);
}

static void
gate_open (Gate *gate)
{
    g_mutex_lock (&gate->mutex);
    gate->closed = FALSE;
    g_cond_broadcast (&gate->cond);
    g_mutex_unlock (&gate->mutex);
}

static void
gate_pass (Gate *gate)
{
    g_mutex_lock (&gate->mutex);
    if (gate->closed && g_thread_self () != gate->spared)
    {
        gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

        g_atomic_int_inc (&gate->held);
        /* Counted off the main thread: see g_paste_test_bus_wait_for_count (). */
        g_main_context_wakeup (NULL);
        while (gate->closed && g_cond_wait_until (&gate->cond, &gate->mutex, deadline))
            ;
        g_assert_false (gate->closed);
    }
    g_mutex_unlock (&gate->mutex);
}

/* Every stream write failing while set, as a full disk would fail it, and
 * how many did; and every one held at @write_gate while it is closed. */
static gboolean fail_writes;
static guint    failed_writes;
static Gate     write_gate;

gboolean
g_output_stream_write_all (GOutputStream *stream,
                           const void    *buffer,
                           gsize          count,
                           gsize         *bytes_written,
                           GCancellable  *cancellable,
                           GError       **error)
{
    typedef gboolean (*WriteAll) (GOutputStream *, const void *, gsize, gsize *, GCancellable *, GError **);
    WriteAll original = (WriteAll) dlsym (RTLD_NEXT, "g_output_stream_write_all");

    g_assert_nonnull (original);
    gate_pass (&write_gate);

    if (fail_writes)
    {
        g_atomic_int_inc (&failed_writes);
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "no space this time");
        return FALSE;
    }

    return original (stream, buffer, count, bytes_written, cancellable, error);
}

/* A directory whose listings are counted in @listings_of, the one numbered
 * @failed_listing failing as a directory that cannot be read: %NULL for none,
 * and 0 for no failure. */
static gchar *listed_dir;
static guint  listings_of;
static guint  failed_listing;

GFileEnumerator *
g_file_enumerate_children (GFile              *file,
                           const char         *attributes,
                           GFileQueryInfoFlags flags,
                           GCancellable       *cancellable,
                           GError            **error)
{
    typedef GFileEnumerator *(*Enumerate) (GFile *, const char *, GFileQueryInfoFlags, GCancellable *, GError **);
    Enumerate original = (Enumerate) dlsym (RTLD_NEXT, "g_file_enumerate_children");
    g_autofree gchar *path = (listed_dir) ? g_file_get_path (file) : NULL;

    g_assert_nonnull (original);
    if (path && g_paste_str_equal (path, listed_dir) && ++listings_of == failed_listing)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "refused this time");
        return NULL;
    }

    return original (file, attributes, flags, cancellable, error);
}

/* The history loads handed to a thread, counted as they go: a load that has to
 * wait is told apart from one that has not run yet, however slow the worker.
 * While @keep_loads is set, the last of them is kept in @kept_load, which says
 * when it is done. */
static guint           loads_started;
static gboolean        keep_loads;
static GTask          *kept_load;
/* With @defer_lock_waits, a startup gate's wait for the storage lock is kept
 * here rather than handed to a thread, as a pool with every thread busy keeps
 * it, until run_deferred_lock_wait (). With @release_after_lock_wait, it is run
 * there and then, and the process lets go of the lock once it has answered,
 * before the gate's callback can run. */
static gboolean        defer_lock_waits;
static GTask          *deferred_lock_wait;
static GTaskThreadFunc deferred_lock_func;
static gboolean        release_after_lock_wait;

void
g_task_run_in_thread (GTask          *task,
                      GTaskThreadFunc task_func)
{
    typedef void (*Run) (GTask *, GTaskThreadFunc);
    Run original = (Run) dlsym (RTLD_NEXT, "g_task_run_in_thread");

    g_assert_nonnull (original);
    if (g_paste_str_equal (g_task_get_name (task), "gpaste-history-load"))
    {
        g_atomic_int_inc (&loads_started);
        if (keep_loads)
            g_set_object (&kept_load, task);
    }
    if (defer_lock_waits && g_paste_str_equal (g_task_get_name (task), "gpaste-storage-lock-wait"))
    {
        g_assert_null (deferred_lock_wait);
        deferred_lock_wait = g_object_ref (task);
        deferred_lock_func = task_func;
        return;
    }
    if (release_after_lock_wait && g_paste_str_equal (g_task_get_name (task), "gpaste-storage-lock-wait"))
    {
        release_after_lock_wait = FALSE;
        task_func (task, g_task_get_source_object (task), g_task_get_task_data (task), g_task_get_cancellable (task));
        g_paste_storage_backend_unlock ();
        return;
    }
    original (task, task_func);
}

static gboolean
task_completed (gconstpointer task,
                gconstpointer arg G_GNUC_UNUSED)
{
    return g_task_get_completed ((GTask *) task);
}

static void
run_deferred_lock_wait (void)
{
    typedef void (*Run) (GTask *, GTaskThreadFunc);
    Run original = (Run) dlsym (RTLD_NEXT, "g_task_run_in_thread");
    g_autoptr (GTask) task = g_steal_pointer (&deferred_lock_wait);

    g_assert_nonnull (task);
    defer_lock_waits = FALSE;
    original (task, deferred_lock_func);
}

/* Moves whose source ends with one of these fail, as a filesystem giving out
 * would fail them: NULL for none. */
static const gchar * const *failed_moves;
/* The deletes of a file whose path ends with this fail, as a directory left
 * read-only would fail them: NULL for none. */
static const gchar *failed_delete_suffix;

gboolean
g_file_delete (GFile        *file,
               GCancellable *cancellable,
               GError      **error)
{
    typedef gboolean (*Delete) (GFile *, GCancellable *, GError **);
    Delete original = (Delete) dlsym (RTLD_NEXT, "g_file_delete");
    g_autofree gchar *path = g_file_get_path (file);

    g_assert_nonnull (original);
    if (failed_delete_suffix && path && g_str_has_suffix (path, failed_delete_suffix))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "injected failure deleting %s", path);

        return FALSE;
    }

    return original (file, cancellable, error);
}
/* The files fsync () fails for, by suffix, and every path it synced with its
 * size then (a gint64), which g_file_move () below checks what it installs
 * against: one that has grown since was synced before its last write. And,
 * while a case keeps the two tables, counted on one sequence, when each path
 * was last synced (@synced_at) and when a move last went into each directory
 * (@moved_into): a rename is on the disk once its directory is synced after
 * it. */
static const gchar * const *failed_syncs;
static GHashTable          *synced;
static gboolean             moved_unsynced;
static GHashTable          *synced_at;
static GHashTable          *moved_into;
static guint                io_sequence;

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

        if (synced_at)
            g_hash_table_insert (synced_at, g_strdup (path), GUINT_TO_POINTER (++io_sequence));
        g_hash_table_insert (synced, g_steal_pointer (&path), g_memdup2 (&size, sizeof (size)));
    }

    return original (fd);
}

/* What flock () fails with, as a home on NFS without lockd has it fail: 0 for
 * nothing. And, while @pause_taken_lock is set, the next flock () taking a
 * lock says so through @lock_taken and then holds on to the thread, as one
 * preempted between taking the lock and recording it would: until
 * @lock_taken_asking says the case is asking about that moment -- ten seconds
 * at most, @lock_taken_missed saying the pause gave up first -- and then until
 * @lock_taken_seen says it has its answer, or a second has passed, the case
 * being unable to say so while it waits behind the thread. @busy_flocks counts
 * the exclusive locks refused for being held, a wait for the lock having
 * started once there is one, and @busy_threads the threads they were refused
 * to. */
static gint        refused_flock;
static guint       busy_flocks;
static GHashTable *busy_threads;
G_LOCK_DEFINE_STATIC (busy_threads);
static gint     pause_taken_lock;
static GMutex   lock_taken_mutex;
static GCond    lock_taken_cond;
static gboolean lock_taken;
static gboolean lock_taken_asking;
static gboolean lock_taken_missed;
static gboolean lock_taken_seen;

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
        G_LOCK (busy_threads);
        if (busy_threads)
            g_hash_table_add (busy_threads, g_thread_self ());
        G_UNLOCK (busy_threads);
        /* Counted off the main thread: see g_paste_test_bus_wait_for_count (). */
        g_main_context_wakeup (NULL);
        errno = busy;
    }

    if (!taken && (operation & LOCK_EX) && g_atomic_int_compare_and_exchange (&pause_taken_lock, TRUE, FALSE))
    {
        gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

        g_mutex_lock (&lock_taken_mutex);
        lock_taken = TRUE;
        g_cond_broadcast (&lock_taken_cond);
        while (!lock_taken_asking && g_cond_wait_until (&lock_taken_cond, &lock_taken_mutex, deadline))
            ;
        lock_taken_missed = !lock_taken_asking;
        deadline = g_get_monotonic_time () + G_USEC_PER_SEC;
        while (!lock_taken_missed && !lock_taken_seen && g_cond_wait_until (&lock_taken_cond, &lock_taken_mutex, deadline))
            ;
        g_mutex_unlock (&lock_taken_mutex);
    }

    return taken;
}

/* While @paused_sleeper names a thread, its next g_usleep () -- a wait for the
 * storage lock between two attempts -- is held at @sleep_gate. */
static GThread *paused_sleeper;
static Gate     sleep_gate;

void
g_usleep (gulong microseconds)
{
    typedef void (*Sleep) (gulong);
    Sleep original = (Sleep) dlsym (RTLD_NEXT, "g_usleep");

    g_assert_nonnull (original);
    if (g_atomic_pointer_compare_and_exchange (&paused_sleeper, g_thread_self (), NULL))
        gate_pass (&sleep_gate);

    original (microseconds);
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

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* Whether the copy keeping a history's original aside, on a filesystem without
 * hard links, stops part way and has the next start's settle look at what it
 * left: a process dying in that copy, and the next one starting. And how many
 * copies a re-key made as a file's replacement, which come out a bit off at
 * their end while @rekey_copies_damaged is set. */
static gboolean settle_mid_copy;
static guint    rekey_copies;
static gboolean rekey_copies_damaged;

gboolean
g_file_copy (GFile                *source,
             GFile                *destination,
             GFileCopyFlags        flags,
             GCancellable         *cancellable,
             GFileProgressCallback progress,
             gpointer              data,
             GError              **error)
{
    typedef gboolean (*Copy) (GFile *, GFile *, GFileCopyFlags, GCancellable *, GFileProgressCallback, gpointer, GError **);
    Copy original = (Copy) dlsym (RTLD_NEXT, "g_file_copy");
    g_autofree gchar *from = g_file_get_path (source);
    g_autofree gchar *to = g_file_get_path (destination);

    g_assert_nonnull (original);
    if (settle_mid_copy && strstr (to, ".rekey-old") && g_str_has_suffix (from, ".xmls"))
    {
        g_autofree gchar *contents = NULL;
        gsize length;

        settle_mid_copy = FALSE;
        g_assert_true (g_file_get_contents (from, &contents, &length, NULL));
        g_assert_true (g_file_set_contents (to, contents, length / 2, NULL));
        g_paste_file_backend_settle_rekeys ();
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "injected death copying %s", from);

        return FALSE;
    }

    if (!g_str_has_suffix (to, G_PASTE_FILE_BACKEND_REKEY_NEW))
        return original (source, destination, flags, cancellable, progress, data, error);

    ++rekey_copies;
    if (!original (source, destination, flags, cancellable, progress, data, error))
        return FALSE;

    if (rekey_copies_damaged)
    {
        g_autofree gchar *contents = NULL;
        gsize length;

        g_assert_true (g_file_get_contents (to, &contents, &length, NULL));
        contents[length - 1] ^= 1;
        g_assert_true (g_file_set_contents (to, contents, length, NULL));
    }

    return TRUE;
}
#endif

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

    gboolean moved = original (source, destination, flags, cancellable, progress, data, error);

    if (moved && moved_into)
    {
        g_autofree gchar *destination_path = g_file_get_path (destination);
        g_autofree gchar *destination_dir = (destination_path) ? g_path_get_dirname (destination_path) : NULL;
        g_autofree gchar *real_dir = (destination_dir) ? realpath (destination_dir, NULL) : NULL;

        if (real_dir)
            g_hash_table_insert (moved_into, g_steal_pointer (&real_dir), GUINT_TO_POINTER (++io_sequence));
    }

    return moved;
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

/* Where a statement stepped on another thread than the case's waits while it
 * is closed: a read of the SQLite backend, which holds its lock meanwhile,
 * made as long as the case needs. */
static Gate step_gate;

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
    gate_pass (&step_gate);

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

/* How often the statement @counted_prepare_sql was prepared: NULL for none. */
static const gchar *counted_prepare_sql;
static guint counted_prepares;

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
    if (counted_prepare_sql && !g_strcmp0 (sql, counted_prepare_sql))
        ++counted_prepares;
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
 * @failed_rekey, the history's install fails, and that one alone: a put-back
 * after it goes through. */
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

        if (failed_rekey)
            failed_moves = (rekey_journals->len == failed_rekey) ? history_install : NULL;
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
/* A write through a backend of this kind is where the process dies: it writes
 * nothing, and what the journal said then is kept for the case to put back,
 * as the next start would find it (%NULL for no journal). -1 for none. */
static gint dying_kind = -1;
static gchar *journal_at_death;

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
    if (kind == journaled_kind || kind == dying_kind)
    {
        /* journal_path ()'s, the migration being included further down. */
        g_autofree gchar *dir = g_paste_util_get_history_dir_path ();
        g_autofree gchar *journal = g_build_filename (dir, "storage-journal", NULL);

        journal_at_write = g_file_test (journal, G_FILE_TEST_EXISTS);
        if (kind == dying_kind)
        {
            g_clear_pointer (&journal_at_death, g_free);
            g_file_get_contents (journal, &journal_at_death, NULL, NULL);
            return FALSE;
        }
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

/* The flavour this build is to lack, standing for a build without the
 * feature it needs: -1 for none. The library asks through its own exported
 * symbol, which this one takes the place of. */
static gint hidden_kind = -1;
static guint asked;

gboolean
g_paste_storage_is_supported (GPasteStorage storage_kind)
{
    typedef gboolean (*Supported) (GPasteStorage);
    Supported original = (Supported) dlsym (RTLD_NEXT, "g_paste_storage_is_supported");

    g_assert_nonnull (original);
    ++asked;

    return (gint) storage_kind != hidden_kind && original (storage_kind);
}

/* The folder whose filesystem takes file names of @limited_max bytes at most,
 * as eCryptfs's encrypted names are: %NULL for none. */
static const gchar *limited_dir;
static glong        limited_max;

long
pathconf (const char *path,
          int         name)
{
    typedef long (*Pathconf) (const char *, int);
    Pathconf original = (Pathconf) dlsym (RTLD_NEXT, "pathconf");

    g_assert_nonnull (original);
    if (limited_dir && name == _PC_NAME_MAX && g_paste_str_equal (path, limited_dir))
        return limited_max;

    return original (path, name);
}

/* The thread the last count of a store ran on, while @counts_watched is set;
 * how many counts @counted_name's store had; and every count held at
 * @count_gate once it has counted, while it is closed. */
static gboolean     counts_watched;
static GThread     *counted_on;
static const gchar *counted_name;
static guint        counted_times;
static Gate         count_gate;

gsize
g_paste_storage_backend_count_history (GPasteStorageBackend *self,
                                       const gchar          *name)
{
    typedef gsize (*Count) (GPasteStorageBackend *, const gchar *);
    Count original = (Count) dlsym (RTLD_NEXT, "g_paste_storage_backend_count_history");

    g_assert_nonnull (original);
    if (counts_watched)
        g_atomic_pointer_set (&counted_on, g_thread_self ());
    if (g_paste_str_equal (name, counted_name))
        g_atomic_int_inc (&counted_times);

    gsize count = original (self, name);

    gate_pass (&count_gate);

    return count;
}

#if defined(G_PASTE_ENABLE_ENCRYPTION) || defined(G_PASTE_ENABLE_SQLITE)
/* Whether the library's own calls reach this binary's definitions -- the
 * interposer above, the storage-migration functions its copy built in here
 * defines again -- which they do only while they go through the PLT:
 * -Bsymbolic binds them inside the library, and a case needing them skips
 * itself then. Asked of the interposer, through a call the library makes. The
 * cases needing them hide a flavour that can be missing, count a storage
 * worker in this binary's copy of the counter the library asks
 * g_paste_storage_is_busy () of, or count the reads of the process
 * passphrase; the ListHistories cases, which hold or watch the counts of a
 * store, ask counts_interposed () instead. Every other case interposes calls
 * the library makes into another one, or that the migration makes from its
 * copy, which -Bsymbolic leaves alone. */
static gboolean
library_calls_interposed (void)
{

    guint before = asked;

    g_paste_prompt_can_import (G_PASTE_STORAGE_FILE, G_PASTE_STORAGE_SQLITE);
    if (asked != before)
        return TRUE;

    g_test_skip ("libgpaste-daemon binds its own calls (-Bsymbolic)");

    return FALSE;
}

/* Whether a call libgpaste-daemon makes within one of its files reaches this
 * binary too, which the probe above, a call from another file, cannot tell: a
 * compiler assuming no semantic interposition keeps such a call within the
 * library (see tests/meson.build). */
static gboolean
library_file_calls_interposed (void)
{
#ifdef G_PASTE_TEST_LIBRARY_INLINES_OWN_CALLS
    g_test_skip ("libgpaste-daemon binds the calls it makes within one file");

    return FALSE;
#else
    return library_calls_interposed ();
#endif
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

/* While set, the next whole read of a stream has @renamed_from renamed over
 * @renamed_path once it is done, as a sync tool replacing a file does while
 * it is being read. */
static const gchar *renamed_path;
static const gchar *renamed_from;

gboolean
g_input_stream_read_all (GInputStream *stream,
                         void         *buffer,
                         gsize         count,
                         gsize        *bytes_read,
                         GCancellable *cancellable,
                         GError      **error)
{
    typedef gboolean (*ReadAll) (GInputStream *, void *, gsize, gsize *, GCancellable *, GError **);
    ReadAll original = (ReadAll) dlsym (RTLD_NEXT, "g_input_stream_read_all");
    gboolean read = original (stream, buffer, count, bytes_read, cancellable, error);

    if (renamed_path)
    {
        const gchar *path = renamed_path;

        renamed_path = NULL;
        g_assert_cmpint (g_rename (renamed_from, path), ==, 0);
    }

    return read;
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
    /* The backends the last migration request offered, and how many were made. */
    GPasteStorage        offered[G_PASTE_N_STORAGE];
    guint                n_offered;
    guint                migrations;
    /* How many of those had been made when the last report came. */
    guint                migrations_at_report;
    /* What confirmations answer, how many were asked, and what the last one
     * explained. */
    gboolean             confirm;
    guint                confirms;
    gchar               *confirm_description;
};

static void test_migration_prompt_iface_init (GPastePromptInterface *iface);

G_DEFINE_TYPE_WITH_CODE (TestMigrationPrompt, test_migration_prompt, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_PASTE_TYPE_PROMPT, test_migration_prompt_iface_init))

static void
test_migration_prompt_migration (GPastePrompt        *prompt,
                                 GPastePromptRequest *request)
{
    TestMigrationPrompt *self = TEST_MIGRATION_PROMPT (prompt);
    guint n_offered;
    const GPasteStorage *offered = g_paste_prompt_request_get_offered (request, &n_offered);

    g_assert_cmpuint (n_offered, <=, G_N_ELEMENTS (self->offered));
    memcpy (self->offered, offered, n_offered * sizeof (GPasteStorage));
    self->n_offered = n_offered;
    ++self->migrations;

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
    self->migrations_at_report = self->migrations;
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
test_migration_prompt_confirm (GPastePrompt        *prompt,
                               GPastePromptRequest *request)
{
    TestMigrationPrompt *self = TEST_MIGRATION_PROMPT (prompt);

    ++self->confirms;
    g_set_str (&self->confirm_description, g_paste_prompt_request_get_description (request));

    if (self->confirm)
        g_paste_prompt_request_reply_confirm (request);
    else
        g_paste_prompt_request_dismiss (request);
}

static void
test_migration_prompt_iface_init (GPastePromptInterface *iface)
{
    iface->passphrase = test_migration_prompt_passphrase;
    iface->migration = test_migration_prompt_migration;
    iface->report = test_migration_prompt_report;
    iface->confirm = test_migration_prompt_confirm;
}

static void
test_migration_prompt_finalize (GObject *object)
{
    g_free (TEST_MIGRATION_PROMPT (object)->title);
    g_free (TEST_MIGRATION_PROMPT (object)->message);
    g_free (TEST_MIGRATION_PROMPT (object)->passphrase_error);
    g_free (TEST_MIGRATION_PROMPT (object)->passphrase_description);
    g_free (TEST_MIGRATION_PROMPT (object)->confirm_description);

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

/* Run the migration through @prompt and hand back whether it settled, with
 * @error set when it did not. */
static gboolean
run_migration (GPasteSettings      *settings,
               TestMigrationPrompt *prompt,
               GError             **error)
{
    g_autoptr (GAsyncResult) result = NULL;

    g_paste_storage_migration_async (G_PASTE_PROMPT (prompt), settings, g_paste_test_env_keep_result, &result);
    g_paste_test_env_wait_result_within (&result, G_PASTE_TEST_STORAGE_WAIT_SECONDS);

    return g_paste_storage_migration_finish (result, error);
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* The same, for a change of passphrase and for an unlock. */
static gboolean
run_rekey (GPasteSettings      *settings,
           TestMigrationPrompt *prompt,
           GError             **error)
{
    g_autoptr (GAsyncResult) result = NULL;

    g_paste_storage_rekey_async (G_PASTE_PROMPT (prompt), settings, g_paste_test_env_keep_result, &result);
    g_paste_test_env_wait_result_within (&result, G_PASTE_TEST_STORAGE_WAIT_SECONDS);

    return g_paste_storage_rekey_finish (result, error);
}

static gboolean
run_decryption (GPasteSettings      *settings,
                TestMigrationPrompt *prompt,
                GError             **error)
{
    g_autoptr (GAsyncResult) result = NULL;

    g_paste_storage_decryption_async (G_PASTE_PROMPT (prompt), settings, g_paste_test_env_keep_result, &result);
    g_paste_test_env_wait_result_within (&result, G_PASTE_TEST_STORAGE_WAIT_SECONDS);

    return g_paste_storage_decryption_finish (result, error);
}
#endif

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
static gpointer
read_history_b (gpointer data)
{
    g_autolist (GPasteItem) items = NULL;
    gsize size;

    g_paste_storage_backend_read_history (data, "delete-during-read-b", NULL, &items, &size);
    return NULL;
}

/* For a call that must not wait for the read held: one that does gets it let
 * go two seconds on, and returns late rather than never. */
static gpointer
release_steps_eventually (gpointer data G_GNUC_UNUSED)
{
    gint64 deadline = g_get_monotonic_time () + 2 * G_USEC_PER_SEC;

    g_mutex_lock (&step_gate.mutex);
    while (step_gate.closed && g_cond_wait_until (&step_gate.cond, &step_gate.mutex, deadline))
        ;
    g_mutex_unlock (&step_gate.mutex);
    gate_open (&step_gate);
    return NULL;
}

/* Deleting a history waits for no read of another one: a load holds the SQLite
 * backend's lock across its key derivation and every row, which the delete, on
 * the main loop -- gnome-shell's compositor for the in-shell daemon -- would
 * wait out. */
static void
test_sqlite_delete_during_read (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("stored"));
    g_autofree gchar *deleted = g_paste_storage_backend_get_history_file_path (backend, "delete-during-read-a");

    g_assert_true (g_paste_storage_backend_write_history (backend, "delete-during-read-a", items));
    g_assert_true (g_paste_storage_backend_write_history (backend, "delete-during-read-b", items));

    gate_close (&step_gate);

    g_autoptr (GThread) reader = g_thread_new ("read-b", read_history_b, backend);

    g_paste_test_bus_wait_for_count (&step_gate.held, 1);

    g_autoptr (GThread) release = g_thread_new ("release-steps", release_steps_eventually, NULL);
    g_autoptr (GError) error = NULL;
    gint64 start = g_get_monotonic_time ();

    g_paste_storage_backend_delete_history (backend, "delete-during-read-a", &error);

    gint64 elapsed = g_get_monotonic_time () - start;

    gate_open (&step_gate);
    g_thread_join (g_steal_pointer (&release));
    g_thread_join (g_steal_pointer (&reader));

    g_assert_no_error (error);
    g_assert_false (g_file_test (deleted, G_FILE_TEST_EXISTS));
    g_assert_cmpint (elapsed, <, G_USEC_PER_SEC);

    /* A history of that name written again is written anew. */
    g_assert_true (g_paste_storage_backend_write_history (backend, "delete-during-read-a", items));
    g_assert_true (g_file_test (deleted, G_FILE_TEST_EXISTS));
}
#endif

#ifdef G_PASTE_ENABLE_SQLITE
static gpointer
read_history_forgotten (gpointer data)
{
    g_autolist (GPasteItem) items = NULL;
    gsize size;

    g_paste_storage_backend_read_history (data, "forgotten-log", NULL, &items, &size);
    return NULL;
}

/* Dropping a connection to a history deleted under a read of it checkpoints
 * nothing and unlinks no log, SQLite skipping that for a database unlinked
 * under its connection: a new database of that name written meanwhile -- a
 * backup through a backend of its own -- keeps the commits still in its log,
 * which the last connection closing would otherwise unlink by name. */
static void
test_sqlite_forgotten_spares_new_log (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("stored"));
    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (backend, "forgotten-log");

    g_assert_true (g_paste_storage_backend_write_history (backend, "forgotten-log", items));

    gate_close (&step_gate);

    g_autoptr (GThread) reader = g_thread_new ("read-forgotten", read_history_forgotten, backend);

    g_paste_test_bus_wait_for_count (&step_gate.held, 1);

    g_autoptr (GError) error = NULL;

    g_paste_storage_backend_delete_history (backend, "forgotten-log", &error);
    g_assert_no_error (error);

    gate_open (&step_gate);
    g_thread_join (g_steal_pointer (&reader));

    /* A full write checkpoints what it deleted; an add after it leaves its
     * commit in the log. */
    g_autoptr (GPasteStorageBackend) other = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autoptr (GPasteItem) added = g_paste_text_item_new ("backed up");

    g_assert_true (g_paste_storage_backend_write_history (other, "forgotten-log", items));
    g_paste_storage_backend_add_item (other, "forgotten-log", added, NULL);

    /* The next open drops the connection the delete left forgotten. */
    g_assert_true (g_paste_storage_backend_write_history (backend, "forgotten-log-next", items));
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items WHERE value = 'backed up';"), ==, 1);
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
    g_paste_storage_migration_async (G_PASTE_PROMPT (prompt), settings, g_paste_test_env_keep_result, &result);
    /* As in /storage/rekey/busy. */
    g_paste_test_bus_wait_until (busy_or_answered, &result, NULL);
    g_assert_true (g_paste_storage_is_busy ());
    g_paste_test_env_wait_result_within (&result, G_PASTE_TEST_STORAGE_WAIT_SECONDS);
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
        /* The kind hidden is asked within gpaste-storage-backend.c. */
        if (!library_file_calls_interposed ())
            return;

        G_PASTE_TEST_TRAP_SUBPROCESS ("*This build cannot open the encrypted storage backend*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*did not read back as it was written*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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

/* The search of freed memory finds a secret left behind, whichever way GLib
 * frees the block holding it: through free (), and through free_sized () for
 * what it knows the size of, where the C library has one. What the cases
 * below count as no copy left is one the search would have found. */
static void
test_secret_search_counts (void)
{
    const gchar *secret = "a password left behind";
    gsize size = strlen (secret) + 1;
    gchar *unsized = g_strdup (secret);
    gchar *sized = g_malloc (size);

    memcpy (sized, secret, size);
    freed_holding_secret = 0;
    freed_secret = secret;
    g_free (unsized);
    g_free_sized (sized, size);
    freed_secret = NULL;
    g_assert_cmpuint (freed_holding_secret, ==, 2);
}

/* Neither a write nor a read of an encrypted history leaves a copy of a
 * password it holds in freed memory: the value encoded for the document, the
 * document and, read back, the text the parser hands over and the value
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
    freed_holding_secret = 0;
    freed_secret = secret;
    g_assert_true (g_paste_storage_backend_write_history (backend, name, items));
    freed_secret = NULL;
    g_assert_cmpuint (freed_holding_secret, ==, 0);

    g_autolist (GPasteItem) loaded = NULL;
    gsize size;

    freed_holding_secret = 0;
    freed_secret = secret;
    g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &loaded, &size));
    freed_secret = NULL;
    g_assert_cmpuint (g_list_length (loaded), ==, 2);
    g_assert_cmpuint (freed_holding_secret, ==, 0);
}

/* Nor does a read that fails once part of the document is decrypted: here a
 * document carried on past its last frame, the refusal coming with the last
 * full chunk already handed out. */
static void
test_encrypted_failed_read_leaves_no_copy (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Data past the end of the encrypted stream*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    const gchar *name = "no-copy-left-on-failure";
    const gchar *secret = "a-password-nobody-should-find-in-freed-memory";
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "passphrase");
    g_autofree gchar *filler = g_strnfill (4000, 'f');
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_password_item_new ("name", secret, 0));

    items = g_list_append (items, g_paste_text_item_new (filler));
    g_assert_true (g_paste_storage_backend_write_history (backend, name, items));

    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (backend, name);
    g_autofree gchar *document = NULL;
    gsize length;

    g_assert_true (g_file_get_contents (path, &document, &length, NULL));

    g_autoptr (GByteArray) carried_on = g_byte_array_new ();

    g_byte_array_append (carried_on, (const guint8 *) document, length);
    g_byte_array_append (carried_on, (const guint8 *) "and then some", 13);
    g_assert_true (g_file_set_contents (path, (const gchar *) carried_on->data, carried_on->len, NULL));

    g_autolist (GPasteItem) loaded = NULL;
    gsize size;

    freed_holding_secret = 0;
    freed_secret = secret;
    g_assert_false (g_paste_storage_backend_read_history (backend, name, NULL, &loaded, &size));
    freed_secret = NULL;
    g_assert_cmpuint (freed_holding_secret, ==, 0);
}
#endif

/* The migration offers what this build can open, in the enum's order and no
 * storage last: neither a flavour whose feature is not built, nor one this case
 * hides as if it were not. */
static void
test_migration_offers_supported (void)
{
    static const GPasteStorage expected[] = {
        G_PASTE_STORAGE_FILE,
#ifdef G_PASTE_ENABLE_SQLITE
        G_PASTE_STORAGE_SQLITE,
#ifdef G_PASTE_ENABLE_ENCRYPTION
        G_PASTE_STORAGE_ENCRYPTED_SQLITE,
#endif
#endif
        G_PASTE_STORAGE_NOOP,
    };
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GError) error = NULL;

    g_paste_settings_set_storage_backend_revision (settings, G_PASTE_STORAGE_BACKEND_REVISION - 1);

    hidden_kind = G_PASTE_STORAGE_ENCRYPTED_FILE;
    prompt->dismiss = TRUE;
    g_assert_true (run_migration (settings, prompt, &error));
    g_assert_no_error (error);
    hidden_kind = -1;

    g_assert_cmpmem (prompt->offered, prompt->n_offered * sizeof (GPasteStorage), expected, sizeof (expected));
}

#ifdef G_PASTE_ENABLE_SQLITE
/* A reply choosing a storage it was not offered -- one this build cannot open,
 * a value out of range -- answers nothing, as a dismissal does: the backend
 * standing in for it would import over the files the cleanup deletes. The
 * request it is answered through refuses it, before the migration sees it. */
static void
test_migration_refuses_unoffered_choice (void)
{
    if (!library_calls_interposed ())
        return;

    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*was not offered; dismissing it*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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

/* A build that cannot open a flavour stands the file one in for it, which
 * reaches none of that flavour's stores: deleting or emptying a history one of
 * them keeps is refused, as the "no storage" backend refuses one another
 * flavour keeps (/history/noop_refuses_unmade), rather than answered as done
 * with the history still on disk, back with the next build opening it. Backing
 * one up is refused only when the stand-in keeps none of it: one it keeps is
 * listed, and copied as listed. */
static void
test_unopenable_store_out_of_reach (void)
{
    /* The kind hidden is asked within gpaste-storage-backend.c, by
     * g_paste_storage_backend_reaches_history (). */
    if (!library_file_calls_interposed ())
        return;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) database = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("kept"));
    g_autofree gchar *current = g_paste_storage_backend_get_history_file_path (database, "unopenable-current");
    g_autofree gchar *other = g_paste_storage_backend_get_history_file_path (database, "unopenable-other");
    g_autofree gchar *listed = g_paste_storage_backend_get_history_file_path (database, "unopenable-listed");

    g_assert_true (g_paste_storage_backend_write_history (database, "unopenable-current", items));
    g_assert_true (g_paste_storage_backend_write_history (database, "unopenable-other", items));
    g_assert_true (g_paste_storage_backend_write_history (database, "unopenable-listed", items));
    g_clear_object (&database);
    hidden_kind = G_PASTE_STORAGE_SQLITE;
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);

    g_autoptr (GPasteStorageBackend) stand_in = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    g_autoptr (GPasteDaemon3) skeleton = g_paste_daemon3_skeleton_new ();
    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .skeleton = skeleton };
    g_autoptr (GError) deleted = NULL;
    g_autoptr (GError) emptied = NULL;
    g_autoptr (GError) other_deleted = NULL;

    g_paste_history_load (history, "unopenable-current");
    g_paste_history_add (history, g_paste_text_item_new ("this session"));
    g_paste_storage_backend_delete_history (stand_in, "unopenable-current", &deleted);
    g_assert_error (deleted, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    g_assert_false (g_paste_history_empty (history, &emptied));
    g_assert_error (emptied, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
    /* Unlisted by the stand-in, and out of its reach rather than unknown. */
    g_paste_daemon_methods_delete_history (&methods, "unopenable-other", &other_deleted);
    g_assert_error (other_deleted, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    g_assert_true (g_file_test (current, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (other, G_FILE_TEST_EXISTS));

    g_autoptr (GError) other_backed_up = NULL;
    g_autoptr (GError) listed_backed_up = NULL;
    g_autolist (GPasteItem) plain = g_list_append (NULL, g_paste_text_item_new ("listed"));
    g_autolist (GPasteItem) copied = NULL;
    gsize size;

    backup_history (&methods, "unopenable-other", "unopenable-other-copy", &other_backed_up);
    g_assert_error (other_backed_up, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    g_assert_true (g_paste_storage_backend_write_history (stand_in, "unopenable-listed", plain));
    backup_history (&methods, "unopenable-listed", "unopenable-listed-copy", &listed_backed_up);
    g_assert_no_error (listed_backed_up);
    g_assert_true (g_paste_storage_backend_read_history (stand_in, "unopenable-listed-copy", NULL, &copied, &size));
    g_assert_cmpuint (g_list_length (copied), ==, 1);
    g_assert_cmpstr (g_paste_item_get_value (copied->data), ==, "listed");

    hidden_kind = -1;
    g_paste_history_flush (history);
    g_assert_cmpint (g_unlink (current), ==, 0);
    g_assert_cmpint (g_unlink (other), ==, 0);
    g_assert_cmpint (g_unlink (listed), ==, 0);
    g_paste_storage_backend_delete_history (stand_in, "unopenable-listed", NULL);
    g_paste_storage_backend_delete_history (stand_in, "unopenable-listed-copy", NULL);
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
}

#endif

/* A read given up on before it reached the store creates none: a load a delete
 * of its history superseded would bring that history back, empty. */
static void
test_cancelled_read_creates_nothing (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GCancellable) cancelled = g_cancellable_new ();
    GPasteStorage kinds[] = {
        G_PASTE_STORAGE_FILE,
#ifdef G_PASTE_ENABLE_SQLITE
        G_PASTE_STORAGE_SQLITE,
#endif
    };

    g_cancellable_cancel (cancelled);
    g_assert_true (g_paste_util_ensure_history_dir_exists ());

    for (gsize i = 0; i < G_N_ELEMENTS (kinds); ++i)
    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (kinds[i], settings);
        g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (backend, "given-up");
        g_autolist (GPasteItem) items = NULL;
        gsize size = 0;

        g_paste_storage_backend_read_history (backend, "given-up", cancelled, &items, &size);
        g_assert_null (items);
        g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));
    }
}

/* A history of a flavour this build cannot delete is refused, not skipped: the
 * start taking back what an import left would otherwise read its copies as
 * gone. A kind past the table stands for one, and so do the SQLite flavours in
 * a build without SQLite. */
static void
test_delete_unsupported_refused (void)
{
    GPasteStorage kinds[] = {
        G_PASTE_N_STORAGE,
#ifndef G_PASTE_ENABLE_SQLITE
        G_PASTE_STORAGE_SQLITE,
        G_PASTE_STORAGE_ENCRYPTED_SQLITE,
#endif
    };

    for (gsize i = 0; i < G_N_ELEMENTS (kinds); ++i)
    {
        g_autoptr (GError) error = NULL;

        g_paste_storage_delete_history (kinds[i], "unsupported", &error);
        g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    }
}

/* A new history's name is bounded by what the history directory's filesystem
 * takes -- asked of the folder above it while it is not made yet -- where a
 * name past it would be switched to and filled with nothing of it ever saved. */
static void
test_name_fits_directory (void)
{
    g_autofree gchar *dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *parent = g_path_get_dirname (dir);
    /* With ".xml.tmp" after it, 138 and 148 bytes. */
    g_autofree gchar *fitting = g_strnfill (130, 'a');
    g_autofree gchar *too_long = g_strnfill (140, 'a');

    limited_max = 143;
    g_rmdir (dir);
    g_assert_false (g_file_test (dir, G_FILE_TEST_EXISTS));
    limited_dir = parent;
    g_assert_true (g_paste_storage_name_fits (G_PASTE_STORAGE_FILE, fitting));
    g_assert_false (g_paste_storage_name_fits (G_PASTE_STORAGE_FILE, too_long));

    g_assert_cmpint (g_mkdir_with_parents (dir, 0700), ==, 0);
    limited_dir = dir;
    g_assert_true (g_paste_storage_name_fits (G_PASTE_STORAGE_FILE, fitting));
    g_assert_false (g_paste_storage_name_fits (G_PASTE_STORAGE_FILE, too_long));

    limited_dir = NULL;
    g_assert_true (g_paste_storage_name_fits (G_PASTE_STORAGE_FILE, too_long));
}

#ifdef G_PASTE_ENABLE_SQLITE
/* Neither toggle is offered for a source this build cannot open, and a reply
 * ticking them anyway is clamped: reading the source through the backend that
 * stands in for it would import nothing, and deleting through it would delete
 * another flavour's histories. */
static void
test_migration_clamps_unopenable_source (void)
{
    if (!library_calls_interposed ())
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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*did not read back as it was written*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
    g_paste_settings_set_storage_backend_revision (settings, G_PASTE_STORAGE_BACKEND_REVISION - 1);
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

        g_assert_true (journal_write (JOURNAL_IMPORT, G_PASTE_STORAGE_SQLITE, journaled, NULL, NULL));
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

    G_PASTE_TEST_IN_SUBPROCESS;

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
    g_assert_true (journal_write (JOURNAL_IMPORT, G_PASTE_STORAGE_SQLITE, journaled, NULL, NULL));

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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*could not be read back once written*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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

/* Whether a count of a store reaches the interposer above, the case skipped
 * when it does not: a call from gpaste-history.c into
 * gpaste-storage-backend.c, which -Bsymbolic binds inside the library, and an
 * optimising compiler may inline there (see tests/meson.build). */
static gboolean
counts_interposed (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    /* On the plain flavour, whatever an earlier case left set: an encrypted
     * one's count asks the keyring for the passphrase. */
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    counted_on = NULL;
    counts_watched = TRUE;
    g_paste_history_get_size (history, "counts-probe");
    counts_watched = FALSE;
    if (g_atomic_pointer_get (&counted_on))
        return TRUE;

    g_test_skip ("libgpaste-daemon binds its own calls");

    return FALSE;
}

/* What a ListHistories answer says @name holds. */
static guint64
listed_size (GVariant    *histories,
             const gchar *name)
{
    for (gsize i = 0; i < g_variant_n_children (histories); ++i)
    {
        const gchar *listed = NULL;
        guint64 size = 0;

        g_variant_get_child (histories, i, "(&st)", &listed, &size);
        if (g_paste_str_equal (listed, name))
            return size;
    }
    g_assert_not_reached ();
}

/* The ListHistories cases' histories: "listed-other", a file history holding
 * @texts, and "listed-current", which the history returned loads. */
static GPasteHistory *
listed_histories_new (GPasteSettings       **settings,
                      GPasteStorageBackend **backend,
                      const gchar * const   *texts)
{
    *settings = g_paste_settings_new ();
    g_paste_settings_set_storage_backend (*settings, G_PASTE_STORAGE_FILE);
    *backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, *settings);

    g_autolist (GPasteItem) items = NULL;

    for (const gchar * const *text = texts; *text; ++text)
        items = g_list_append (items, g_paste_text_item_new (*text));
    g_assert_true (g_paste_storage_backend_write_history (*backend, "listed-other", items));

    GPasteHistory *history = g_paste_history_new (*settings);

    g_paste_history_load (history, "listed-current");

    return history;
}

/* Both histories gone, and the storage back to its default. */
static void
listed_histories_clear (GPasteSettings       *settings,
                        GPasteStorageBackend *backend,
                        GPasteHistory        *history)
{
    g_paste_history_flush (history);
    g_paste_storage_backend_delete_history (backend, "listed-other", NULL);
    g_paste_storage_backend_delete_history (backend, "listed-current", NULL);
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
}

/* ListHistories counts every history but the one in use in its store, which
 * for a file history reads its whole document -- and derives a key, for an
 * encrypted one: on a worker thread, where on the main loop it would stall
 * the daemon, gnome-shell for the in-shell one, for as long. The sizes are
 * checked wherever the library binds its own calls, only the thread they were
 * counted on needing the interposer. */
static void
test_list_histories_counted_off_main_loop (void)
{
    gboolean interposed = counts_interposed ();
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = listed_histories_new (&settings, &backend, (const gchar *[]) { "first", "second", NULL });
    const GPasteDaemonMethods methods = { .history = history, .settings = settings };
    g_autoptr (GError) error = NULL;

    g_paste_history_add (history, g_paste_text_item_new ("this session"));
    counted_on = NULL;
    counts_watched = TRUE;

    g_autoptr (GVariant) histories = list_histories (&methods, &error);

    counts_watched = FALSE;
    g_assert_no_error (error);
    g_assert_cmpuint (listed_size (histories, "listed-other"), ==, 2);
    g_assert_cmpuint (listed_size (histories, "listed-current"), ==, 1);
    listed_histories_clear (settings, backend, history);
    if (!interposed)
        return;

    GThread *thread = g_atomic_pointer_get (&counted_on);

    g_assert_nonnull (thread);
    g_assert_true (thread != g_thread_self ());
}

/* A listing sizes the history in use as its answer goes out: what is copied
 * while the other histories are counted is in it, as it is in a
 * GetHistorySize asked after the listing and answered first, whose count the
 * listing would otherwise land over with an older one. */
static void
test_list_histories_current_as_answered (void)
{
    if (!counts_interposed ())
        return;

    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = listed_histories_new (&settings, &backend, (const gchar *[]) { "other", NULL });
    const GPasteDaemonMethods methods = { .history = history, .settings = settings };
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    g_paste_history_add (history, g_paste_text_item_new ("before"));
    gate_close (&count_gate);
    g_paste_daemon_methods_list_histories_async (&methods, g_paste_test_env_keep_result, &result);
    g_paste_test_bus_wait_for_count (&count_gate.held, 1);
    g_paste_history_add (history, g_paste_text_item_new ("while counted"));
    gate_open (&count_gate);
    g_paste_test_env_wait_result (&result);

    g_autoptr (GVariant) histories = g_paste_daemon_methods_list_histories_finish (result, &error);

    g_assert_no_error (error);
    g_assert_cmpuint (listed_size (histories, "listed-current"), ==, 2);
    g_assert_cmpuint (listed_size (histories, "listed-other"), ==, 1);
    listed_histories_clear (settings, backend, history);
}

/* Another history emptied while a listing counts the stores is sized again:
 * counted before the emptying reached it, its store would be answered for as
 * it was after HistoryEmptied has said it holds nothing, which the graphical
 * tool's sidebar takes as the row's count. It alone: every store counted again
 * would hold the answer back for as long as emptyings kept landing in a pass,
 * a key derivation apiece on an encrypted store. It runs in a child only for
 * the storage lock the emptying needs, which would otherwise outlive the case,
 * and so keeps warnings fatal (gpaste-test-history.h). */
static void
test_list_histories_recounted_after_empty (void)
{
    if (!g_test_subprocess ())
    {
        if (!counts_interposed ())
            return;

        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_assert_true (g_paste_storage_backend_try_lock ());

    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = listed_histories_new (&settings, &backend, (const gchar *[]) { "first", "second", NULL });
    g_autolist (GPasteItem) kept = NULL;

    kept = g_list_append (kept, g_paste_text_item_new ("first"));
    kept = g_list_append (kept, g_paste_text_item_new ("second"));
    g_assert_true (g_paste_storage_backend_write_history (backend, "listed-kept", kept));

    g_autoptr (GPasteDaemon3) skeleton = g_paste_daemon3_skeleton_new ();
    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .skeleton = skeleton };
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    counted_name = "listed-kept";
    gate_close (&count_gate);
    g_paste_daemon_methods_list_histories_async (&methods, g_paste_test_env_keep_result, &result);
    g_paste_test_bus_wait_for_count (&count_gate.held, 1);
    empty_history (&methods, "listed-other", &error);
    g_assert_no_error (error);
    gate_open (&count_gate);
    g_paste_test_env_wait_result (&result);

    g_autoptr (GVariant) histories = g_paste_daemon_methods_list_histories_finish (result, &error);

    g_assert_no_error (error);
    g_assert_cmpuint (listed_size (histories, "listed-other"), ==, 0);
    g_assert_cmpuint (listed_size (histories, "listed-kept"), ==, 2);
    g_assert_cmpuint (g_atomic_int_get (&counted_times), ==, 1);
}

/* So with the history in use left while the stores are counted: noted as it
 * was when the count began, it would be answered for with what it held then,
 * though by the time the answer goes out it is neither in use nor writing, and
 * its store holds more. */
static void
test_list_histories_recounted_after_leave (void)
{
    if (!counts_interposed ())
        return;

    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    g_autoptr (GPasteHistory) history = listed_histories_new (&settings, &backend, (const gchar *[]) { "other", NULL });
    const GPasteDaemonMethods methods = { .history = history, .settings = settings };
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    g_paste_history_add (history, g_paste_text_item_new ("before"));
    gate_close (&count_gate);
    g_paste_daemon_methods_list_histories_async (&methods, g_paste_test_env_keep_result, &result);
    g_paste_test_bus_wait_for_count (&count_gate.held, 1);
    g_paste_history_add (history, g_paste_text_item_new ("while counted"));
    g_paste_history_load (history, "listed-next");
    g_paste_history_flush (history);
    gate_open (&count_gate);
    g_paste_test_env_wait_result (&result);

    g_autoptr (GVariant) histories = g_paste_daemon_methods_list_histories_finish (result, &error);

    g_assert_no_error (error);
    g_assert_cmpuint (listed_size (histories, "listed-current"), ==, 2);
    listed_histories_clear (settings, backend, history);
    g_paste_storage_backend_delete_history (backend, "listed-next", NULL);
}

/* A history let go of while a listing counts its stores goes there and then,
 * the listing answering what it counted: held until the counts were over, the
 * in-shell daemon's history would outlive its dispose, its items with it, and
 * still follow the settings the successor writes. */
static void
test_list_histories_history_dropped (void)
{
    if (!counts_interposed ())
        return;

    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteStorageBackend) backend = NULL;
    GPasteHistory *history = listed_histories_new (&settings, &backend, (const gchar *[]) { "other", NULL });
    const GPasteDaemonMethods methods = { .history = history, .settings = settings };
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    g_paste_history_flush (history);
    g_object_add_weak_pointer (G_OBJECT (history), (gpointer *) &history);
    gate_close (&count_gate);
    g_paste_daemon_methods_list_histories_async (&methods, g_paste_test_env_keep_result, &result);
    g_paste_test_bus_wait_for_count (&count_gate.held, 1);
    g_object_unref (history);
    g_assert_null (history);
    gate_open (&count_gate);
    g_paste_test_env_wait_result (&result);

    g_autoptr (GVariant) histories = g_paste_daemon_methods_list_histories_finish (result, &error);

    g_assert_no_error (error);
    g_assert_cmpuint (listed_size (histories, "listed-other"), ==, 1);
    /* As listed_histories_clear (), the history being gone already. */
    g_paste_storage_backend_delete_history (backend, "listed-other", NULL);
    g_paste_storage_backend_delete_history (backend, "listed-current", NULL);
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
}

/* The file flavour's store lags a history whose last save failed too: the
 * flush an exit makes writes it level, where the successor would load what
 * the last save that landed left. */
static void
test_file_diverged_history (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*no space this time*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "file-diverged-history";

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_assert_true (g_paste_storage_backend_try_lock ());
    g_paste_history_load (history, name);

    failed_writes = 0;
    fail_writes = TRUE;
    gate_close (&write_gate);
    /* Two writes, both failing: the second recorded while the first is out,
     * which waits for it, and so queued behind it. Nothing comes after them:
     * every write of this flavour is a full one, which no failure has made
     * again before the next change. */
    g_paste_history_add (history, g_paste_text_item_new ("a"));
    g_paste_test_bus_wait_for_count (&write_gate.held, 1);
    g_paste_history_add (history, g_paste_text_item_new ("b"));
    gate_open (&write_gate);
    g_paste_test_bus_wait_for_count (&failed_writes, 2);
    fail_writes = FALSE;

    /* Room again, and nothing copied since: the flush an exit makes writes
     * the store level. */
    g_paste_history_flush (history);

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autolist (GPasteItem) read = NULL;
    gsize size = 0;

    g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &read, &size));
    g_assert_cmpuint (g_list_length (read), ==, 2);
}

#ifdef G_PASTE_ENABLE_SQLITE
#ifdef G_PASTE_ENABLE_ENCRYPTION
static void
key_and_write_meanwhile (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) other = g_paste_sqlite_backend_new_encrypted (settings, "passphrase");
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("written meanwhile"));

    g_assert_true (g_paste_storage_backend_write_history (other, "keyed-meanwhile", items));
}

/* A database another connection keys, and writes to, while this one derives
 * a key for it is read again: its parameters are the ones to take, where
 * replacing them would leave what it wrote under a key nothing derives. */
static void
test_sqlite_keyed_meanwhile (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, "passphrase");
    g_autolist (GPasteItem) none = NULL;
    gsize size = 0;

    during_derivation = key_and_write_meanwhile;
    g_assert_true (g_paste_storage_backend_read_history (backend, "keyed-meanwhile", NULL, &none, &size));
    g_assert_null (during_derivation);

    g_autoptr (GPasteStorageBackend) reader = g_paste_sqlite_backend_new_encrypted (settings, "passphrase");
    g_autolist (GPasteItem) read = NULL;

    g_assert_true (g_paste_storage_backend_read_history (reader, "keyed-meanwhile", NULL, &read, &size));
    g_assert_cmpuint (g_list_length (read), ==, 1);
    g_assert_cmpstr (g_paste_item_get_value (read->data), ==, "written meanwhile");
}
#endif

/* Moving an item to the front binds none of its content again, where an
 * insert would bind its value, image and alternatives -- sealing them afresh,
 * in the encrypted flavour -- only for the conflict to keep nothing of them: a
 * bind of the value, refused, would leave the item out. */
static void
test_sqlite_select_moves_only (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    const gchar *name = "select-moves-only";
    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (backend, name);
    g_autoptr (GPasteItem) selected = g_paste_text_item_new ("a value longer than the uuid it is stored under");
    g_autoptr (GPasteItem) other = g_paste_text_item_new ("other");
    g_autoptr (GList) history = g_list_prepend (g_list_prepend (NULL, selected), other);

    /* A full write tries no move, the table it fills just emptied. */
    counted_prepare_sql = "UPDATE items SET rank = ?, favourite = ?, timeout = ? WHERE uuid = ?;";
    counted_prepares = 0;
    g_paste_storage_backend_write_history (backend, name, history);
    g_assert_cmpuint (counted_prepares, ==, 0);

    refused_bind_length = strlen (g_paste_item_get_uuid (selected));
    g_paste_storage_backend_add_item (backend, name, selected, NULL);
    refused_bind_length = 0;
    g_assert_cmpuint (counted_prepares, ==, 1);
    counted_prepare_sql = NULL;
    g_assert_false (g_paste_storage_backend_has_diverged (backend, name));
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items;"), ==, 2);
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items WHERE value = 'other' AND rank = (SELECT MIN (rank) FROM items);"), ==, 1);
}

/* A delete's checkpoint is put off by a reader holding the log rather than
 * waited for: the backend's lock is held all along, and every write of the
 * store would sit out the busy timeout behind it. */
static void
test_sqlite_checkpoint_spares_readers (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    const gchar *name = "checkpoint-reader";
    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (backend, name);
    g_autoptr (GPasteItem) a = g_paste_text_item_new ("a");
    g_autoptr (GPasteItem) b = g_paste_text_item_new ("b");
    g_autoptr (GList) history = g_list_prepend (g_list_prepend (NULL, a), b);
    g_autoptr (sqlite3) reader = NULL;

    g_paste_storage_backend_write_history (backend, name, history);
    g_assert_cmpint (sqlite3_open (path, &reader), ==, SQLITE_OK);
    g_assert_cmpint (sqlite3_exec (reader, "BEGIN; SELECT COUNT (*) FROM items;", NULL, NULL, NULL), ==, SQLITE_OK);

    gint64 start = g_get_monotonic_time ();

    history = g_list_remove (history, b);
    g_paste_storage_backend_remove_item (backend, name, g_paste_item_get_uuid (b), history);
    g_assert_cmpint (g_get_monotonic_time () - start, <, G_USEC_PER_SEC);

    g_assert_cmpint (sqlite3_exec (reader, "COMMIT;", NULL, NULL, NULL), ==, SQLITE_OK);
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items;"), ==, 1);
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* A re-key empties the write-ahead log once committed, the live history's
 * backend holding a connection of its own: the database file alone, its log
 * lost with a process dying, would otherwise open under the passphrase being
 * retired. */
static void
test_sqlite_rekey_checkpoints (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) live = g_paste_sqlite_backend_new_encrypted (settings, "old");
    const gchar *name = "rekey-checkpoints";
    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (live, name);
    g_autofree gchar *wal = g_strconcat (path, "-wal", NULL);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("secret"));
    GStatBuf wal_stat;

    g_assert_true (g_paste_storage_backend_write_history (live, name, items));

    g_autoptr (GPasteStorageBackend) rekeyer = g_paste_sqlite_backend_new_encrypted (settings, "old");
    gboolean split = TRUE;

    g_assert_true (g_paste_storage_backend_rekey (rekeyer, name, "new", &split));
    g_assert_false (split);
    g_assert_true (g_stat (wal, &wal_stat) || !wal_stat.st_size);
}
#endif

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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*injected disk-full failure*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*injected disk-full failure*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*failed to reconcile an item*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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

    G_PASTE_TEST_IN_SUBPROCESS;

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

/* An emptying that goes through leaves the store level with the history, as a
 * full write does, whatever failed before it -- the database emptied, or one
 * that would not open replaced: the next add then needs no full write. */
static void
test_sqlite_emptied_converges (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autoptr (GPasteItem) a = g_paste_text_item_new ("a");
    g_autoptr (GList) history = g_list_prepend (NULL, a);
    const gchar *name = "emptied-converges";
    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (backend, name);

    g_assert_true (g_paste_storage_backend_write_history (backend, name, history));
    failed_prepare_sql = "DELETE FROM items WHERE uuid = ?;";
    g_paste_storage_backend_remove_item (backend, name, g_paste_item_get_uuid (a), NULL);
    g_assert_null (failed_prepare_sql);
    g_assert_true (g_paste_storage_backend_has_diverged (backend, name));
    g_paste_storage_backend_clear_history (backend, name, NULL);
    g_assert_false (g_paste_storage_backend_has_diverged (backend, name));
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items;"), ==, 0);

    g_autoptr (GPasteStorageBackend) other = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);

    g_assert_true (g_file_set_contents (path, "no database", -1, NULL));
    g_paste_storage_backend_remove_item (other, name, g_paste_item_get_uuid (a), NULL);
    g_assert_true (g_paste_storage_backend_has_diverged (other, name));
    g_paste_storage_backend_clear_history (other, name, NULL);
    g_assert_false (g_paste_storage_backend_has_diverged (other, name));
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items;"), ==, 0);
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

/* An add carrying the history onto a store some write left behind writes it
 * whole, which levels the store, and leaves it behind still when that write
 * fails too: the add records its own full write, which no wrapper sees. */
static void
test_sqlite_diverged_add_converges (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*injected disk-full failure*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autoptr (GPasteItem) a = g_paste_text_item_new ("a");
    g_autoptr (GPasteItem) b = g_paste_text_item_new ("b");
    g_autoptr (GList) history = g_list_prepend (NULL, a);
    const gchar *name = "diverged-add-converges";
    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (backend, name);

    g_assert_true (g_paste_storage_backend_write_history (backend, name, history));
    failed_prepare_sql = "DELETE FROM items WHERE uuid = ?;";
    g_paste_storage_backend_remove_item (backend, name, g_paste_item_get_uuid (a), NULL);
    g_assert_null (failed_prepare_sql);
    g_assert_true (g_paste_storage_backend_has_diverged (backend, name));

    history = g_list_prepend (history, b);
    full_writes = 0;
    failed_sql = "DELETE FROM items;";
    g_paste_storage_backend_add_item (backend, name, b, history);
    failed_sql = NULL;
    g_assert_cmpuint (full_writes, ==, 1);
    g_assert_true (g_paste_storage_backend_has_diverged (backend, name));

    g_paste_storage_backend_add_item (backend, name, b, history);
    g_assert_cmpuint (full_writes, ==, 2);
    g_assert_false (g_paste_storage_backend_has_diverged (backend, name));
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items;"), ==, 2);
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

/* An item left out as too large is one the model holds and the store does
 * not: a displacing add's count takes it away, or the row the add displaced
 * would make up for it and stay; and an edit of it, which no row stands for,
 * has the next full write put it in. */
static void
test_sqlite_left_out_accounted (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*too large to store*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    const gchar *name = "left-out-accounted";
    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (backend, name);
    g_autofree gchar *large = g_strnfill (2048, 'x');
    g_autoptr (GPasteItem) big = g_paste_text_item_new (large);
    g_autoptr (GPasteItem) a = g_paste_text_item_new ("a");
    g_autoptr (GPasteItem) b = g_paste_text_item_new ("b");
    g_autoptr (GPasteItem) x = g_paste_text_item_new ("x");
    g_autoptr (GList) history = g_list_prepend (g_list_prepend (g_list_prepend (NULL, b), a), big);

    refused_bind_length = 1024;
    g_paste_storage_backend_write_history (backend, name, history);
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items;"), ==, 2);

    /* x in, b displaced. */
    history = g_list_remove (history, b);
    history = g_list_prepend (history, x);
    g_paste_storage_backend_add_item (backend, name, x, history);
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items;"), ==, 2);
    g_assert_cmpint (sqlite_query_int (path, "SELECT COUNT (*) FROM items WHERE value = 'b';"), ==, 0);
    g_assert_false (g_paste_storage_backend_has_diverged (backend, name));

    g_autoptr (GPasteItem) edited = g_paste_text_item_new ("edited");

    g_list_find (history, big)->data = edited;
    g_paste_storage_backend_replace_item (backend, name, g_paste_item_get_uuid (big), edited, history);
    g_assert_true (g_paste_storage_backend_has_diverged (backend, name));
    refused_bind_length = 0;
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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*too large to store*");
        g_test_trap_assert_stderr ("*failed to write an item*");
        g_test_trap_assert_stderr ("*failed to write a special value: out of memory*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*injected disk-full failure*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
     * worker. Then two backups, each landing behind whatever was recorded
     * before it and failing as one write does: a third write the full one
     * asked for, recorded once its answer is in, would land before the
     * first if that answer was in by then, and before the second
     * otherwise. */
    g_paste_test_bus_wait_for_count (&injected_failures, 2);

    g_autoptr (GPasteDaemon3) skeleton = g_paste_daemon3_skeleton_new ();
    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .skeleton = skeleton };

    for (guint i = 0; i < 2; ++i)
    {
        g_autofree gchar *backup = g_strdup_printf ("diverged-history-%u", i);
        g_autoptr (GError) error = NULL;

        backup_history (&methods, name, backup, &error);
        g_assert_nonnull (error);
    }
    g_assert_cmpuint (injected_failures, ==, 4);

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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*injected disk-full failure*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
    backup_history (&methods, "answered", "answered-backup", &error);
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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*already holds a history*it cannot read*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*injected failure moving*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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

/* A failed import whose rollback cannot delete a copy it wrote keeps that copy
 * journaled, for the next start to take back -- and says so -- rather than
 * leaving it on the disk with nothing to reach it. */
static void
test_import_rollback_keeps_undeleted (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*injected failure deleting*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "left-behind";
    g_autofree gchar *copy = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_SQLITE));
    g_autofree gchar *journal = journal_path ();

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);

    {
        g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("first"));

        items = g_list_append (items, g_paste_text_item_new ("second"));
        g_assert_true (g_paste_storage_backend_write_history (file, name, items));
    }

    g_autoptr (GError) error = NULL;

    shortened_kind = G_PASTE_STORAGE_SQLITE;
    failed_delete_suffix = ".db";
    g_assert_false (import_histories (settings, G_PASTE_STORAGE_FILE, NULL, G_PASTE_STORAGE_SQLITE, NULL, &error));
    shortened_kind = -1;
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    g_assert_nonnull (strstr (error->message, "at the next start"));
    g_assert_true (g_file_test (copy, G_FILE_TEST_EXISTS));

    JournalOperation operation;
    GPasteStorage kind;
    g_auto (GStrv) names = NULL;
    g_autofree gchar *under_way = NULL;

    g_assert_true (journal_read (&operation, &kind, &names, &under_way));
    g_assert_null (under_way);
    g_assert_cmpint (operation, ==, JOURNAL_IMPORT);
    g_assert_cmpint (kind, ==, G_PASTE_STORAGE_SQLITE);
    g_assert_cmpstrv (names, ((const gchar * const []) { name, NULL }));

    /* The journal kept by the next start's settle, which the delete still
     * fails for, is no import's to write over: one is refused meanwhile. */
    g_paste_storage_decryption_needed (settings);
    g_assert_true (g_file_test (journal, G_FILE_TEST_EXISTS));
    g_clear_error (&error);
    g_assert_false (import_histories (settings, G_PASTE_STORAGE_FILE, NULL, G_PASTE_STORAGE_SQLITE, NULL, &error));
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    g_clear_pointer (&names, g_strfreev);
    g_assert_true (journal_read (&operation, &kind, &names, &under_way));
    g_assert_cmpstrv (names, ((const gchar * const []) { name, NULL }));

    /* Nor is it a migration's to remove that imports nothing: "Keep as is". */
    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);

    g_clear_error (&error);
    g_paste_settings_set_storage_backend_revision (settings, G_PASTE_STORAGE_BACKEND_REVISION - 1);
    prompt->chosen = G_PASTE_STORAGE_FILE;
    g_assert_true (run_migration (settings, prompt, &error));
    g_assert_no_error (error);
    g_assert_true (g_file_test (journal, G_FILE_TEST_EXISTS));

    failed_delete_suffix = NULL;
    g_paste_storage_decryption_needed (settings);
    g_assert_false (g_file_test (copy, G_FILE_TEST_EXISTS));
    g_assert_false (g_file_test (journal, G_FILE_TEST_EXISTS));
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
}

/* An item a SQLite destination leaves out as too large for a row is excused
 * by the import's read-back as a plain destination's password is: the import
 * goes through, every other item copied. */
static void
test_import_excuses_left_out (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*too large to store*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "left-out-imported";
    g_autofree gchar *large = g_strnfill (2048, 'x');
    g_autofree gchar *copy = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_SQLITE));

    {
        g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new (large));

        items = g_list_append (items, g_paste_text_item_new ("small"));
        g_assert_true (g_paste_storage_backend_write_history (file, name, items));
    }

    g_autoptr (GError) error = NULL;

    refused_bind_length = 1024;
    g_assert_true (import_histories (settings, G_PASTE_STORAGE_FILE, NULL, G_PASTE_STORAGE_SQLITE, NULL, &error));
    refused_bind_length = 0;
    g_assert_no_error (error);
    g_assert_cmpint (sqlite_query_int (copy, "SELECT COUNT (*) FROM items;"), ==, 1);
}

/* A process dying while an import writes over a copy the destination already
 * held leaves that copy for the next start to keep: the journal names only
 * what the import created, the next start deleting what it names, and the
 * copy is as it was or, written, one of what the source still holds. */
static void
test_import_death_spares_kept_copy (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    const gchar *name = "kept-copy";

    {
        g_autoptr (GPasteStorageBackend) sqlite = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
        g_autolist (GPasteItem) old = g_list_append (NULL, g_paste_text_item_new ("older copy"));
        g_autolist (GPasteItem) source = g_list_append (NULL, g_paste_text_item_new ("source"));

        g_assert_true (g_paste_storage_backend_write_history (file, name, old));
        g_assert_true (g_paste_storage_backend_write_history (sqlite, name, source));
    }

    g_autoptr (GError) error = NULL;

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_SQLITE);
    dying_kind = G_PASTE_STORAGE_FILE;
    g_assert_false (import_histories (settings, G_PASTE_STORAGE_SQLITE, NULL, G_PASTE_STORAGE_FILE, NULL, &error));
    dying_kind = -1;

    /* What the next start finds: the journal as the death left it. */
    if (journal_at_death)
    {
        g_autofree gchar *journal = journal_path ();

        g_assert_true (g_file_set_contents (journal, journal_at_death, -1, NULL));
        g_clear_pointer (&journal_at_death, g_free);
    }
    g_paste_storage_decryption_needed (settings);

    g_autolist (GPasteItem) items = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (file, name, NULL, &items, &size));
    g_assert_cmpuint (g_list_length (items), ==, 1);
    g_assert_cmpstr (g_paste_item_get_value (items->data), ==, "older copy");
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
}

/* A destination history whose write went in without an image it could not
 * write, which a file flavour does over a history already there, is the
 * import's copy, and the rollback takes it back with the rest. */
static void
test_import_takes_back_partial_destination (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Failed to create images directory*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    const gchar *name = "partial";
    const guchar pixel[] = { 85, 86, 87, 255 };
    g_autoptr (GBytes) bytes = g_bytes_new (pixel, sizeof (pixel));
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (1, 1, GDK_MEMORY_R8G8B8A8, bytes, 4);
    g_autofree gchar *images = g_paste_file_backend_images_dir (name);
    g_autofree gchar *parent = g_path_get_dirname (images);
    g_autofree gchar *document = g_paste_util_get_history_file_path (name, "xml");

    g_paste_settings_set_images_support (settings, TRUE);

    {
        g_autoptr (GPasteStorageBackend) sqlite = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
        g_autolist (GPasteItem) old = g_list_append (NULL, g_paste_text_item_new ("older copy"));
        g_autolist (GPasteItem) source = g_list_append (NULL, g_paste_text_item_new ("source"));

        source = g_list_prepend (source, g_paste_image_item_new (texture));
        g_assert_true (g_paste_storage_backend_write_history (file, name, old));
        g_assert_true (g_paste_storage_backend_write_history (sqlite, name, source));
    }

    /* What the image's directory cannot be made through. */
    g_assert_cmpint (g_mkdir_with_parents (parent, 0700), ==, 0);
    g_assert_true (g_file_set_contents (images, "", 0, NULL));

    g_autoptr (GError) error = NULL;

    g_assert_false (import_histories (settings, G_PASTE_STORAGE_SQLITE, NULL, G_PASTE_STORAGE_FILE, NULL, &error));
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    g_assert_false (g_file_test (document, G_FILE_TEST_EXISTS));
}

/* A version 1 database whose upgrade fails at the statement @data names has
 * to come out of it exactly as it went in, so that the next open upgrades it
 * whole. */
static void
test_schema_failure (gconstpointer data)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*injected disk-full failure*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*could not read the schema version*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*does not unlock*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*incomplete encryption parameters*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
 * Delete blaming another daemon. Not by a wait counted before this process
 * let go of the lock, though: a gate decided then ends with nothing done. */
static void
test_lock_file_unusable (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Failed to open the history lock file: *");
        g_test_trap_assert_stderr ("*Failed to lock the history: *");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    g_autofree gchar *dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *lock = g_build_filename (dir, "lock", NULL);

    g_assert_true (g_paste_util_ensure_history_dir_exists ());
    g_assert_cmpint (g_mkdir (lock, 0700), ==, 0);
    g_assert_true (g_paste_storage_backend_try_lock ());
    g_assert_true (g_paste_storage_backend_lock (NULL));

    guint releases = g_paste_storage_backend_get_releases ();

    g_paste_storage_backend_unlock ();
    g_assert_false (g_paste_storage_backend_lock_since (releases, NULL));

    /* The same of one flock () refuses. */
    g_assert_cmpint (g_rmdir (lock), ==, 0);
    refused_flock = ENOLCK;
    g_assert_true (g_paste_storage_backend_try_lock ());
    g_assert_true (g_paste_storage_backend_lock (NULL));
    releases = g_paste_storage_backend_get_releases ();
    g_paste_storage_backend_unlock ();
    g_assert_false (g_paste_storage_backend_lock_since (releases, NULL));
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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Could not create history dir*");
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

/* A journal with nothing on its first line -- empty, or starting with a NUL
 * byte -- records nothing this GPaste can act on, and goes as any such one
 * does. */
static void
test_journal_empty (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Dropping*records nothing*Dropping*records nothing*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    g_autofree gchar *path = journal_path ();
    static const gchar nul_first[] = "\0import 1\nhistory\n";
    const struct { const gchar *contents; gssize length; } journals[] = {
        { "", 0 },
        { nul_first, sizeof (nul_first) - 1 },
    };

    g_assert_true (g_paste_util_ensure_history_dir_exists ());
    for (guint i = 0; i < G_N_ELEMENTS (journals); ++i)
    {
        JournalOperation operation;
        GPasteStorage storage_kind;
        g_auto (GStrv) names = NULL;
        g_autofree gchar *under_way = NULL;

        g_assert_true (g_file_set_contents (path, journals[i].contents, journals[i].length, NULL));
        g_assert_false (journal_read (&operation, &storage_kind, &names, &under_way));
        g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));
    }
}

/* A journal that cannot be written, its directory not there to be made, fails
 * saying why: the import or the change of passphrase it is written for reports
 * that rather than an error nobody set. */
static void
test_journal_unwritable (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Could not create history dir*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_ERROR);

    g_autofree gchar *dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *parent = g_path_get_dirname (dir);
    g_autoptr (GPtrArray) names = g_ptr_array_new ();
    g_autoptr (GError) error = NULL;

    g_assert_false (g_file_test (dir, G_FILE_TEST_EXISTS));
    g_assert_cmpint (g_mkdir_with_parents (parent, 0500), ==, 0);
    g_assert_cmpint (g_chmod (parent, 0500), ==, 0);
    g_ptr_array_add (names, "history");
    g_assert_false (journal_write (JOURNAL_IMPORT, G_PASTE_STORAGE_FILE, names, NULL, &error));
    g_assert_nonnull (error);
    g_assert_cmpint (g_chmod (parent, 0700), ==, 0);
}

/* A FIFO named like a history is no history: left out of every listing, and a
 * count, a read or a passphrase probe of its name answers at once rather than
 * waiting in open(2) for a writer that never comes -- on the main loop, which
 * is the compositor's for the in-shell daemon. Run with a deadline, so that a
 * probe blocking fails the case rather than hanging the suite. */
static void
test_fifo_history (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 10 * G_USEC_PER_SEC, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    static const GPasteStorage kinds[] = {
        G_PASTE_STORAGE_FILE,
#ifdef G_PASTE_ENABLE_ENCRYPTION
        G_PASTE_STORAGE_ENCRYPTED_FILE,
#endif
#ifdef G_PASTE_ENABLE_SQLITE
        G_PASTE_STORAGE_SQLITE,
#ifdef G_PASTE_ENABLE_ENCRYPTION
        G_PASTE_STORAGE_ENCRYPTED_SQLITE,
#endif
#endif
    };
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_assert_true (g_paste_util_ensure_history_dir_exists ());
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_paste_storage_backend_set_passphrase ("passphrase");
#endif
    g_paste_settings_set_history_name (settings, "fifo");
    for (guint i = 0; i < G_N_ELEMENTS (kinds); ++i)
    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (kinds[i], settings);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("kept"));
        g_autofree gchar *fifo = g_paste_storage_backend_get_history_file_path (backend, "fifo");

        g_assert_true (g_paste_storage_backend_write_history (backend, "kept", items));
        g_assert_cmpint (mkfifo (fifo, 0600), ==, 0);

        g_autoptr (GError) error = NULL;
        g_auto (GStrv) listed = g_paste_storage_backend_list_histories (backend, &error);

        g_assert_no_error (error);
        g_assert_true (g_strv_contains ((const gchar * const *) listed, "kept"));
        g_assert_false (g_strv_contains ((const gchar * const *) listed, "fifo"));
        g_assert_cmpuint (g_paste_storage_backend_count_history (backend, "fifo"), ==, 0);
#ifdef G_PASTE_ENABLE_ENCRYPTION
        g_paste_settings_set_storage_backend (settings, kinds[i]);
        g_paste_storage_passphrase_verifiable (kinds[i], settings);
        g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
#endif

        if (!g_paste_storage_backend_is_incremental (backend))
        {
            g_autolist (GPasteItem) read = NULL;
            gsize size;

            g_assert_false (g_paste_storage_backend_read_history (backend, "fifo", NULL, &read, &size));
        }

        g_assert_cmpint (g_unlink (fifo), ==, 0);
        g_paste_storage_backend_delete_history (backend, "kept", NULL);
    }
    g_paste_settings_reset (settings, G_PASTE_HISTORY_NAME_SETTING);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_paste_storage_backend_set_passphrase (NULL);
#endif
}

/* A full write that does not land sweeps nothing: what is on disk is still the
 * snapshot before it, which references the image the failed one dropped. */
static void
test_failed_write_keeps_images (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Failed to install history file*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Failed to create images directory*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Failed to install history file*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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

/* The process passphrase set again to its own cleartext, as
 * g_paste_storage_backend_get_passphrase () hands it out, stays what it was. */
static void
test_passphrase_set_again (void)
{
    g_paste_storage_backend_set_passphrase ("passphrase");
    g_paste_storage_backend_set_passphrase (g_paste_storage_backend_get_passphrase ());
    g_assert_cmpstr (g_paste_storage_backend_get_passphrase (), ==, "passphrase");
    g_paste_storage_backend_set_passphrase (NULL);
}

/* A backend keyed with a passphrase of its own reads nothing of the process's,
 * which the main thread may be setting while a storage worker builds one --
 * its plain kind through g_paste_storage_backend_new () included. The read is
 * made within gpaste-storage-backend.c, which the interposer counts only where
 * this build leaves such a call to the PLT: one backend keyed with the
 * process's passphrase shows it counting, the case skipping itself where it
 * does not. */
static void
test_keyed_backend_reads_no_process_passphrase (void)
{
    if (!library_calls_interposed ())
        return;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const GPasteStorage kinds[] = {
        G_PASTE_STORAGE_ENCRYPTED_FILE,
#ifdef G_PASTE_ENABLE_SQLITE
        G_PASTE_STORAGE_ENCRYPTED_SQLITE,
#endif
    };

    g_paste_storage_backend_set_passphrase ("process");
    g_atomic_int_set (&process_passphrase_reads, 0);
    g_object_unref (g_paste_storage_backend_new (G_PASTE_STORAGE_ENCRYPTED_FILE, settings));
    if (!g_atomic_int_get (&process_passphrase_reads))
    {
        g_paste_storage_backend_set_passphrase (NULL);
        g_test_skip ("libgpaste-daemon binds the calls it makes within one file");
        return;
    }

    g_atomic_int_set (&process_passphrase_reads, 0);
    for (gsize i = 0; i < G_N_ELEMENTS (kinds); ++i)
    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (kinds[i], settings, "own");

        g_assert_cmpint (g_paste_storage_backend_get_kind (backend), ==, kinds[i]);
    }
    g_assert_cmpuint (g_atomic_int_get (&process_passphrase_reads), ==, 0);
    g_paste_storage_backend_set_passphrase (NULL);
}

/* A passphrase whose key cannot be derived -- its memory refused -- is not
 * checked, and is refused rather than taken for right: whichever history
 * holds the data, and whichever flavour. */
static void
test_passphrase_verify_underived (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Could not check the passphrase*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    GPasteStorage flavours[] = {
        G_PASTE_STORAGE_ENCRYPTED_FILE,
#ifdef G_PASTE_ENABLE_SQLITE
        G_PASTE_STORAGE_ENCRYPTED_SQLITE,
#endif
    };

    for (gsize i = 0; i < G_N_ELEMENTS (flavours); ++i)
    {
        /* The active history holding the data, then another one alone. */
        for (guint other = 0; other < 2; ++other)
        {
            g_autofree gchar *name = g_strdup_printf ("underived-%" G_GSIZE_FORMAT "-%u", i, other);

            g_paste_settings_set_history_name (settings, (other) ? "underived-none" : name);

            {
                g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (flavours[i], settings, "right");
                g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("precious"));

                g_assert_true (g_paste_storage_backend_write_history (backend, name, items));
            }

            refuse_derivations = TRUE;
            g_assert_false (g_paste_storage_passphrase_can_decrypt (flavours[i], settings, "wrong"));
            refuse_derivations = FALSE;
            g_assert_false (g_paste_storage_passphrase_can_decrypt (flavours[i], settings, "wrong"));
            g_assert_true (g_paste_storage_passphrase_can_decrypt (flavours[i], settings, "right"));

            g_autoptr (GError) error = NULL;

            g_paste_storage_delete_history (flavours[i], name, &error);
            g_assert_no_error (error);
        }
    }

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
/* An active history damaged past its first chunk, which neither refutes nor
 * confirms a passphrase, is asked each once: a key derived for each ask, where
 * asking it again among the others would derive one more. */
static void
test_passphrase_damaged_active_asked_once (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (G_PASTE_STORAGE_ENCRYPTED_FILE, settings, "passphrase");
    g_autofree gchar *long_text = g_strnfill (3 * 4096, 'x');
    g_autolist (GPasteItem) damaged = g_list_append (NULL, g_paste_text_item_new (long_text));

    g_paste_storage_backend_write_history (backend, "damaged-active", damaged);

    g_autofree gchar *path = g_paste_util_get_history_file_path ("damaged-active", "xmls");
    g_autofree gchar *contents = NULL;
    gsize length = 0;
    /* As in test_passphrase_survives_damage (). */
    const gsize second_chunk = 64 + 4 + 4096 + 17 + 4;

    g_assert_true (g_file_get_contents (path, &contents, &length, NULL));
    contents[second_chunk + 16] ^= 1;
    g_assert_true (g_file_set_contents (path, contents, (gssize) length, NULL));
    g_paste_settings_set_history_name (settings, "damaged-active");

    g_atomic_int_set (&derivations, 0);
    g_assert_true (g_paste_storage_passphrase_can_decrypt (G_PASTE_STORAGE_ENCRYPTED_FILE, settings, "passphrase"));
    g_assert_cmpint (g_atomic_int_get (&derivations), <=, 2);

    g_paste_settings_reset (settings, G_PASTE_HISTORY_NAME_SETTING);
    g_assert_cmpint (g_unlink (path), ==, 0);
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
        backup_history (&methods, "history", name, &error);
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
/* Whether a history holds a whole first frame -- what makes a passphrase
 * verifiable -- is answered of one file: a file renamed over it while it is
 * looked at pairs no frame length with another file's size. */
static void
test_has_frame_reads_one_file (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "passphrase");
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("framed"));
    g_autofree gchar *path = g_paste_util_get_history_file_path ("frame-probe", "xmls");
    g_autofree gchar *replacement = g_strconcat (path, ".replacement", NULL);

    g_assert_true (g_paste_storage_backend_write_history (backend, "frame-probe", items));
    g_assert_true (g_file_set_contents (replacement, "", 0, NULL));
    renamed_path = path;
    renamed_from = replacement;
    g_assert_true (g_paste_storage_passphrase_verifiable (G_PASTE_STORAGE_ENCRYPTED_FILE, settings));
    renamed_path = NULL;
    g_unlink (replacement);
    g_assert_cmpint (g_unlink (path), ==, 0);
}

/* A stream damaged in its third frame reads its first two before the error:
 * the call meeting the damage has written the second's plaintext, which it
 * hands back, the failure coming with the next call. */
static void
test_stream_error_after_progress (void)
{
    g_autofree gchar *plaintext = g_strnfill (10000, 'p');
    g_autoptr (GConverter) encrypt = g_paste_secret_stream_converter_new (G_PASTE_SECRET_STREAM_ENCRYPT, "passphrase");
    g_autoptr (GInputStream) plain = g_memory_input_stream_new_from_data (plaintext, 10000, NULL);
    g_autoptr (GInputStream) encrypting = g_converter_input_stream_new (plain, encrypt);
    g_autoptr (GOutputStream) sink = g_memory_output_stream_new_resizable ();
    g_autoptr (GError) error = NULL;

    g_assert_cmpint (g_output_stream_splice (sink, encrypting, G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET, NULL, &error), >, 0);
    g_assert_no_error (error);

    g_autoptr (GBytes) sealed = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (sink));
    gsize length = 0;
    g_autofree guint8 *damaged = g_memdup2 (g_bytes_get_data (sealed, &length), g_bytes_get_size (sealed));
    /* As in test_passphrase_survives_damage (): the third frame's ciphertext. */
    const gsize third_chunk = 64 + 2 * (4 + 4096 + 17) + 4;

    g_assert_cmpuint (length, >, third_chunk + 16);
    damaged[third_chunk + 16] ^= 1;

    g_autoptr (GConverter) decrypt = g_paste_secret_stream_converter_new (G_PASTE_SECRET_STREAM_DECRYPT, "passphrase");
    g_autoptr (GInputStream) sealed_in = g_memory_input_stream_new_from_data (damaged, length, NULL);
    g_autoptr (GInputStream) decrypting = g_converter_input_stream_new (sealed_in, decrypt);
    guint8 buffer[3000];
    gsize total = 0;
    gssize got;

    while ((got = g_input_stream_read (decrypting, buffer, sizeof (buffer), NULL, &error)) > 0)
        total += (gsize) got;

    g_assert_cmpint (got, ==, -1);
    g_assert_nonnull (error);
    g_assert_cmpuint (total, ==, 2 * 4096);
}

/* An encrypted history goes through the converter's fixed buffers a frame at a
 * time, many chunks of it reading back whole; carried on past its FINAL frame
 * -- bytes appended, or a second stream after the first -- it is refused rather
 * than read as its first stream alone. */
static void
test_encrypted_stream_bounds (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Data past the end of the encrypted stream*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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

/* Whether two waits have been refused the lock: see
 * test_storage_lock_two_waits (). */
static gboolean
both_waits_refused (gconstpointer data G_GNUC_UNUSED,
                    gconstpointer arg  G_GNUC_UNUSED)
{
    guint refused;

    G_LOCK (busy_threads);
    refused = g_hash_table_size (busy_threads);
    G_UNLOCK (busy_threads);

    return refused == 2;
}

/* A startup gate touches the store only once this process holds the storage
 * lock: a daemon taken over from goes on rewriting it -- a re-key, an import --
 * until its workers are done, and a passphrase verified or a backend migrated
 * meanwhile would be checked against a store about to change. Letting go of
 * the lock while a gate waits ends it with nothing done. */
static void
test_gates_wait_for_lock (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;
    gint fd = hold_storage_lock ();

    g_paste_settings_set_storage_backend_revision (settings, G_PASTE_STORAGE_BACKEND_REVISION - 1);
    prompt->dismiss = TRUE;
    g_paste_storage_migration_async (G_PASTE_PROMPT (prompt), settings, g_paste_test_env_keep_result, &result);
    g_paste_test_bus_pump (500);
    g_assert_null (result);

    g_paste_storage_backend_unlock ();
    g_paste_test_env_wait_result_within (&result, G_PASTE_TEST_STORAGE_WAIT_SECONDS);
    g_assert_true (g_paste_storage_migration_finish (result, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (prompt->migrations, ==, 0);
    g_clear_object (&result);

    /* Asked once the holder lets go. */
    g_paste_storage_migration_async (G_PASTE_PROMPT (prompt), settings, g_paste_test_env_keep_result, &result);
    g_paste_test_bus_pump (500);
    g_assert_null (result);
    g_assert_cmpuint (prompt->migrations, ==, 0);
    close (fd);
    g_paste_test_env_wait_result_within (&result, G_PASTE_TEST_STORAGE_WAIT_SECONDS);
    g_assert_true (g_paste_storage_migration_finish (result, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (prompt->migrations, ==, 1);
    g_clear_object (&result);

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* No passphrase is verified against it meanwhile: the keyring's is asked
     * once the lock is had, its lookup warning on this bus, which
     * /storage/lock/decryption-gate-asks-keyring counts. */
    G_PASTE_TEST_IN_SUBPROCESS;
    g_paste_storage_backend_unlock ();
    fd = hold_storage_lock ();
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_assert_true (g_paste_storage_decryption_needed (settings));
    prompt->passphrase = "passphrase";
    g_paste_storage_decryption_async (G_PASTE_PROMPT (prompt), settings, g_paste_test_env_keep_result, &result);
    g_paste_test_bus_pump (500);
    g_assert_null (result);
    g_assert_cmpuint (prompt->passphrase_asks, ==, 0);
    close (fd);
    g_paste_test_env_wait_result_within (&result, G_PASTE_TEST_STORAGE_WAIT_SECONDS);
    g_assert_true (g_paste_storage_decryption_finish (result, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (prompt->passphrase_asks, >, 0);
    g_paste_storage_backend_set_passphrase (NULL);
#else
    close (fd);
#endif
}

/* Letting go of the lock after a gate decided to wait for it, but before a
 * thread got to the wait, ends it all the same: the in-shell daemon stopping
 * with the pool's threads all busy, gnome-shell then holding the lock for as
 * long as it runs, and a gate going on with a store no longer its own. With
 * @data, the lock is taken back meanwhile by a later wait of the process -- a
 * runner re-enabled, whose own gate counts from the release: the gate decided
 * before it ends with nothing done all the same, rather than go on alongside
 * the later one. */
static void
test_gate_released_before_wait (gconstpointer data)
{
    gboolean relocked = GPOINTER_TO_INT (data);

    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    g_paste_settings_set_storage_backend_revision (settings, G_PASTE_STORAGE_BACKEND_REVISION - 1);
    prompt->dismiss = TRUE;
    defer_lock_waits = TRUE;
    g_paste_storage_migration_async (G_PASTE_PROMPT (prompt), settings, g_paste_test_env_keep_result, &result);
    g_paste_storage_backend_unlock ();
    if (relocked)
        g_assert_true (g_paste_storage_backend_lock (NULL));
    run_deferred_lock_wait ();
    g_paste_test_env_wait_result_within (&result, G_PASTE_TEST_STORAGE_WAIT_SECONDS);
    g_assert_true (g_paste_storage_migration_finish (result, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (prompt->migrations, ==, 0);

    /* Held by nothing, the later wait's let go of: another file of this
     * process's can take it. */
    if (relocked)
        g_paste_storage_backend_unlock ();
    close (hold_storage_lock ());
}

/* And so does letting go of it once the wait has answered, before the gate's
 * callback runs -- the turn of the main loop between the two, which the
 * in-shell daemon may stand down in: the gate neither acts on the lock it was
 * answered, no longer the process's, nor takes it back. */
static void
test_gate_released_after_wait (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    g_paste_settings_set_storage_backend_revision (settings, G_PASTE_STORAGE_BACKEND_REVISION - 1);
    prompt->dismiss = TRUE;
    release_after_lock_wait = TRUE;
    g_paste_storage_migration_async (G_PASTE_PROMPT (prompt), settings, g_paste_test_env_keep_result, &result);
    g_assert_false (release_after_lock_wait);
    g_assert_null (result);
    g_paste_test_env_wait_result_within (&result, G_PASTE_TEST_STORAGE_WAIT_SECONDS);
    g_assert_true (g_paste_storage_migration_finish (result, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (prompt->migrations, ==, 0);

    /* Not held: another file of this process's can take it. */
    gint fd = hold_storage_lock ();

    close (fd);
}

static gpointer
wait_for_lock_pausing (gpointer data)
{
    g_atomic_pointer_set (&paused_sleeper, g_thread_self ());

    return wait_for_lock (data);
}

/* Letting go of the lock while a wait sleeps between two attempts ends it
 * too, its holder gone and a later wait of the process having taken the lock
 * back before it tries again: finding the process holding the lock, the wait
 * answers that it has none. */
static void
test_lock_released_then_relocked_while_waiting (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    gint held = hold_storage_lock ();
    LockWait wait = { NULL, FALSE, FALSE };

    gate_close (&sleep_gate);

    GThread *thread = g_thread_new ("lock-wait", wait_for_lock_pausing, &wait);

    g_paste_test_bus_wait_for_count (&sleep_gate.held, 1);
    g_close (held, NULL);
    g_paste_storage_backend_unlock ();
    g_assert_true (g_paste_storage_backend_lock (NULL));
    gate_open (&sleep_gate);
    g_thread_join (thread);
    g_assert_false (wait.locked);
    g_paste_storage_backend_unlock ();
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

    G_LOCK (busy_threads);
    busy_threads = g_hash_table_new (NULL, NULL);
    G_UNLOCK (busy_threads);

    for (guint i = 0; i < 2; ++i)
        threads[i] = g_thread_new ("lock-wait", wait_for_lock, &waits[i]);

    /* Both waits under way, each refused by the holder, before it lets go: a
     * wait coming after the other has installed the lock finds it held at the
     * top of g_paste_storage_backend_lock (), and says nothing of the race
     * this case is about. */
    g_paste_test_bus_wait_until (both_waits_refused, NULL, NULL);
    g_assert_true (both_waits_refused (NULL, NULL));
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

    G_LOCK (busy_threads);
    g_clear_pointer (&busy_threads, g_hash_table_unref);
    G_UNLOCK (busy_threads);
}

/* The lock a wait of this process has just taken is the process's: asked
 * then -- a Backup, an Empty or a Delete -- try_lock () is not refused by the
 * process's own lock, as if another daemon held it. Asked while the wait is
 * held between the two, which goes on once try_lock () has answered or a
 * second after it was asked: try_lock () waits that second out where the wait
 * is made under the lock's mutex, as it is to be. A pause that gave up before
 * try_lock () was asked fails the case, which then asked about nothing. */
static void
test_taken_lock_is_ours (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 30 * G_USEC_PER_SEC, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    gint held = hold_storage_lock ();
    gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
    gboolean taken;

    g_paste_history_load_async (history, "taken");
    g_atomic_int_set (&pause_taken_lock, TRUE);
    g_close (held, NULL);

    g_mutex_lock (&lock_taken_mutex);
    while (!lock_taken && g_cond_wait_until (&lock_taken_cond, &lock_taken_mutex, deadline))
        ;
    taken = lock_taken;
    g_mutex_unlock (&lock_taken_mutex);
    g_assert_true (taken);

    g_mutex_lock (&lock_taken_mutex);
    lock_taken_asking = TRUE;
    g_cond_broadcast (&lock_taken_cond);
    g_mutex_unlock (&lock_taken_mutex);
    g_assert_true (g_paste_storage_backend_try_lock ());

    gboolean missed;

    g_mutex_lock (&lock_taken_mutex);
    missed = lock_taken_missed;
    lock_taken_seen = TRUE;
    g_cond_broadcast (&lock_taken_cond);
    g_mutex_unlock (&lock_taken_mutex);
    g_assert_false (missed);
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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Could not read the history back*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
    /* Its wait under way before the release, however late its worker started,
     * and seen as soon as it is: the count is bumped off the main context,
     * which a wait not woken would sleep through to its deadline. */
    gint64 waited = g_get_monotonic_time ();

    g_paste_test_bus_wait_for_count (&busy_flocks, 1);
    g_assert_cmpint (g_get_monotonic_time () - waited, <, 5 * G_USEC_PER_SEC);
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
    busy_flocks = 0;
    keep_loads = TRUE;
    g_paste_history_load_async (history, "dropped-loading");
    /* Dropped once the load is waiting, which is what this is about: a
     * history dropped before its load got to the lock says nothing of it. */
    g_paste_test_bus_wait_for_count (&busy_flocks, 1);
    keep_loads = FALSE;
    g_assert_nonnull (kept_load);
    g_object_unref (history);
    g_assert_null (history);
    /* The load given up while the lock is still held: nothing is left to
     * take it once its holder lets go. */
    g_paste_test_bus_wait_until (task_completed, kept_load, NULL);
    g_assert_true (g_task_get_completed (kept_load));
    g_clear_object (&kept_load);
    g_close (held, NULL);
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

    backup_history (&methods, "other", "other-backup", &error);
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
    backup_history (&methods, "other", "other-backup", &error);
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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Could not read the history back*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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

        G_PASTE_TEST_TRAP_SUBPROCESS ("*Could not read the history back*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Failed to materialize image*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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

static void
write_file (const gchar *path,
            const gchar *contents)
{
    g_autofree gchar *dir = g_path_get_dirname (path);

    g_assert_cmpint (g_mkdir_with_parents (dir, 0700), ==, 0);
    g_assert_true (g_file_set_contents (path, contents, -1, NULL));
}

/* What a write or a delete cut short left under a name no history has goes
 * at the next start, the plain flavour's screenshots and a save's temporary
 * holding the history in clear; what a stored history has stays. */
static void
test_unstored_leftovers_swept (void)
{
    /* Once per process, as every start's gate sweeps. */
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("kept"));
    const gchar *image = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef.png";
    g_autofree gchar *stored_images = g_paste_file_backend_images_dir ("leftovers-stored");
    g_autofree gchar *unstored_images = g_paste_file_backend_images_dir ("leftovers-unstored");
    g_autofree gchar *stored_image = g_build_filename (stored_images, image, NULL);
    g_autofree gchar *unstored_image = g_build_filename (unstored_images, image, NULL);
    g_autofree gchar *stored_document = g_paste_util_get_history_file_path ("leftovers-stored", "xml");
    g_autofree gchar *stored_tmp = g_strconcat (stored_document, ".tmp", NULL);
    g_autofree gchar *unstored_tmp = g_paste_util_get_history_file_path ("leftovers-unstored", "xml.tmp");
    g_autofree gchar *unstored_encrypted_tmp = g_paste_util_get_history_file_path ("leftovers-unstored", "xmls.tmp");

    g_assert_true (g_paste_storage_backend_write_history (file, "leftovers-stored", items));
    write_file (stored_image, "an image");
    write_file (stored_tmp, "a save cut short");
    write_file (unstored_image, "a screenshot");
    write_file (unstored_tmp, "a first save cut short");
    write_file (unstored_encrypted_tmp, "another");

    g_paste_storage_decryption_needed (settings);

    g_assert_true (g_file_test (stored_image, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (stored_tmp, G_FILE_TEST_EXISTS));
    g_assert_false (g_file_test (unstored_images, G_FILE_TEST_EXISTS));
    g_assert_false (g_file_test (unstored_tmp, G_FILE_TEST_EXISTS));
    g_assert_false (g_file_test (unstored_encrypted_tmp, G_FILE_TEST_EXISTS));
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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*injected failure moving*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Could not put back*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
        g_autofree gchar *images_dir = g_path_get_dirname (image);
        g_autofree gchar *real_images_dir = realpath (images_dir, NULL);
        const gchar *originals[] = { history, image };
        GStatBuf before[G_N_ELEMENTS (originals)];
        gboolean split = TRUE;

        for (gsize i = 0; i < G_N_ELEMENTS (originals); ++i)
            g_assert_cmpint (g_stat (originals[i], &before[i]), ==, 0);

        moved_unsynced = FALSE;
        refused_links = without_links;
        g_clear_pointer (&synced, g_hash_table_unref);
        synced_at = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
        moved_into = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
        g_assert_true (g_paste_storage_backend_rekey (backend, name, "new passphrase", &split));
        refused_links = FALSE;
        g_assert_false (split);
        g_assert_false (moved_unsynced);
        /* The renames are on the disk once their directory is synced after
         * them, the image's in the images directory. */
        g_assert_nonnull (real_dir);
        g_assert_nonnull (real_images_dir);
        g_assert_cmpuint (GPOINTER_TO_UINT (g_hash_table_lookup (moved_into, real_dir)), >, 0);
        g_assert_cmpuint (GPOINTER_TO_UINT (g_hash_table_lookup (synced_at, real_dir)), >,
                          GPOINTER_TO_UINT (g_hash_table_lookup (moved_into, real_dir)));
        g_assert_cmpuint (GPOINTER_TO_UINT (g_hash_table_lookup (moved_into, real_images_dir)), >, 0);
        g_assert_cmpuint (GPOINTER_TO_UINT (g_hash_table_lookup (synced_at, real_images_dir)), >,
                          GPOINTER_TO_UINT (g_hash_table_lookup (moved_into, real_images_dir)));
        g_clear_pointer (&synced_at, g_hash_table_unref);
        g_clear_pointer (&moved_into, g_hash_table_unref);

        /* And an original copied aside is on the disk, whole, before the
         * replacement goes over it. */
        for (gsize i = 0; without_links && i < G_N_ELEMENTS (originals); ++i)
        {
            g_autofree gchar *dir = g_path_get_dirname (originals[i]);
            g_autofree gchar *real = realpath (dir, NULL);
            g_autofree gchar *base = g_path_get_basename (originals[i]);
            g_autofree gchar *aside_name = g_strconcat (base, G_PASTE_FILE_BACKEND_REKEY_OLD, NULL);
            g_autofree gchar *aside = g_build_filename (real, aside_name, NULL);
            const gint64 *size = g_hash_table_lookup (synced, aside);

            g_assert_nonnull (size);
            g_assert_cmpint (*size, ==, before[i].st_size);
        }
    }

    g_assert_false (has_rekey_leftovers (name));

    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "new passphrase");
    g_autolist (GPasteItem) items = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &items, &size));
    g_assert_cmpuint (g_list_length (items), ==, 2);
    g_assert_nonnull (g_paste_image_item_get_png_bytes (G_PASTE_IMAGE_ITEM (items->next->data)));
}

/* A re-key cut short while it copies an original aside, on a filesystem without
 * hard links, leaves nothing a settle could take for that original: the history
 * comes out of the next start's settle as it was. */
static void
test_rekey_copy_cut_short (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Could not keep the original of*injected death copying*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "rekey-copy-cut-short";
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_encrypted_history (settings, name, "old passphrase", &history, &image);

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "old passphrase");
        gboolean split = TRUE;

        refused_links = TRUE;
        settle_mid_copy = TRUE;
        g_assert_false (g_paste_storage_backend_rekey (backend, name, "new passphrase", &split));
        refused_links = FALSE;
        g_assert_false (settle_mid_copy);
        g_assert_false (split);
    }

    g_assert_false (has_rekey_leftovers (name));

    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "old passphrase");
    g_autolist (GPasteItem) items = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &items, &size));
    g_assert_cmpuint (g_list_length (items), ==, 2);
    g_assert_nonnull (g_paste_image_item_get_png_bytes (G_PASTE_IMAGE_ITEM (items->next->data)));
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

    G_PASTE_TEST_IN_SUBPROCESS;

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
 * place with a second name of its own and its replacement beside it -- and
 * another image not reached yet, its replacement still beside it. */
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

    g_autofree gchar *untouched = g_build_filename (images, "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210.pngs", NULL);
    g_autofree gchar *untouched_replacement = g_strconcat (untouched, ".rekey", NULL);

    write_file (untouched, "untouched image");
    write_file (untouched_replacement, "its replacement");
}

/* A settle whose listing of the images fails settles nothing: the originals
 * already renamed over stay aside, the history's replacement saying so to the
 * next settle, which puts them back. */
static void
test_rekey_settle_unlisted (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Could not list the images*to settle*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    const gchar *name = "rekey-settle-unlisted";
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_interrupted_rekey (name, &history, &image);

    g_autofree gchar *image_aside = g_strconcat (image, ".rekey-old", NULL);
    g_autofree gchar *replacement = g_strconcat (history, ".rekey", NULL);

    listed_dir = g_paste_file_backend_images_dir (name);
    listings_of = 0;
    failed_listing = 1;
    g_paste_file_backend_settle_rekeys ();
    failed_listing = 0;
    g_clear_pointer (&listed_dir, g_free);
    g_assert_true (g_file_test (image_aside, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (replacement, G_FILE_TEST_EXISTS));

    g_paste_file_backend_settle_rekeys ();
    g_assert_false (has_rekey_leftovers (name));
    assert_contents (image, "old image");
}

/* A settle lists a history's images once, finding there both what it puts
 * back and what is beside it: both startup gates settle every encrypted file
 * history, on the main loop of a host that may be the compositor. */
static void
test_rekey_settle_lists_once (void)
{
    const gchar *name = "rekey-settle-lists-once";
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_interrupted_rekey (name, &history, &image);

    listed_dir = g_paste_file_backend_images_dir (name);
    listings_of = 0;
    g_paste_file_backend_settle_rekeys ();
    g_clear_pointer (&listed_dir, g_free);
    g_assert_cmpuint (listings_of, ==, 1);
    g_assert_false (has_rekey_leftovers (name));
    assert_contents (image, "old image");
}

/* A write that fails over a temporary an earlier one left behind leaves none:
 * the stream writing beside that temporary puts its partial write back over
 * the name when it closes, which the drop waits for. */
static void
test_failed_write_leaves_no_temporary (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    const gchar *name = "failed-write-temporary";
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("never written"));
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "xml");
    g_autofree gchar *tmp = g_strconcat (path, ".tmp", NULL);
    g_autofree gchar *dir = g_path_get_dirname (path);

    g_assert_true (g_paste_util_ensure_history_dir_exists ());
    write_file (tmp, "left by a write cut short");
    fail_writes = TRUE;
    g_assert_false (g_paste_storage_backend_write_history (backend, name, items));
    fail_writes = FALSE;
    g_assert_false (g_file_test (tmp, G_FILE_TEST_EXISTS));

    g_autoptr (GDir) listing = g_dir_open (dir, 0, NULL);

    for (const gchar *entry; (entry = g_dir_read_name (listing));)
        g_assert_false (g_str_has_prefix (entry, ".goutputstream-"));
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

/* An image the passphrase in use cannot read -- damaged, or under another
 * key -- is already lost to its history: a change of passphrase goes through
 * without it, rather than failing for good over a file no window shows, and
 * the file goes with the passphrase it may be under. */
static void
test_rekey_past_unreadable_image (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Failed to read*with the current passphrase*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "rekey-past-unreadable";
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;
    g_autofree gchar *other_history = NULL;
    g_autofree gchar *other_image = NULL;

    write_encrypted_history (settings, name, "old passphrase", &history, &image);
    write_encrypted_history (settings, "rekey-other-key", "another passphrase", &other_history, &other_image);

    g_autofree gchar *images = g_paste_file_backend_images_dir (name);
    g_autofree gchar *damaged = g_build_filename (images, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef.pngs", NULL);
    g_autofree gchar *foreign = g_build_filename (images, "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210.pngs", NULL);
    g_autoptr (GBytes) foreign_bytes = read_bytes (other_image);

    write_file (damaged, "no stream of ours");
    write_bytes (foreign, foreign_bytes);

    /* One already on the passphrase the change goes to, as a change cut short
     * leaves a history split between two: kept as it is. */
    g_autofree gchar *new_history = NULL;
    g_autofree gchar *new_image = NULL;

    write_encrypted_history (settings, "rekey-new-key", "new passphrase", &new_history, &new_image);

    g_autofree gchar *on_new = g_build_filename (images, "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff.pngs", NULL);
    g_autoptr (GBytes) on_new_bytes = read_bytes (new_image);

    write_bytes (on_new, on_new_bytes);

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "old passphrase");
        gboolean split = TRUE;

        g_assert_true (g_paste_storage_backend_rekey (backend, name, "new passphrase", &split));
        g_assert_false (split);
    }

    g_assert_false (has_rekey_leftovers (name));
    g_assert_false (g_file_test (damaged, G_FILE_TEST_EXISTS));
    g_assert_false (g_file_test (foreign, G_FILE_TEST_EXISTS));

    g_autoptr (GBytes) kept = read_bytes (on_new);

    g_assert_true (g_bytes_equal (kept, on_new_bytes));

    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "new passphrase");
    g_autolist (GPasteItem) items = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL, &items, &size));
    g_assert_cmpuint (g_list_length (items), ==, 2);
    g_assert_nonnull (g_paste_image_item_get_png_bytes (G_PASTE_IMAGE_ITEM (items->next->data)));
}

/* What a re-key left beside an image a write's sweep has since taken is
 * settled too: dropped once the re-key went through, put back while it is
 * still to be rolled back. */
static void
test_rekey_settled_without_image (void)
{
    const gchar *names[] = { "rekey-image-gone-forward", "rekey-image-gone-back" };

    for (guint i = 0; i < G_N_ELEMENTS (names); ++i)
    {
        g_autofree gchar *images = g_paste_file_backend_images_dir (names[i]);
        g_autofree gchar *history = g_paste_util_get_history_file_path (names[i], g_paste_storage_get_extension (G_PASTE_STORAGE_ENCRYPTED_FILE));
        g_autofree gchar *image = g_build_filename (images, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef.pngs", NULL);
        g_autofree gchar *image_aside = g_strconcat (image, ".rekey-old", NULL);
        g_autofree gchar *other = g_build_filename (images, "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210.pngs", NULL);
        g_autofree gchar *other_replacement = g_strconcat (other, ".rekey", NULL);
        g_autofree gchar *history_replacement = g_strconcat (history, ".rekey", NULL);
        gboolean pending = i == 1;

        write_file (history, "history");
        /* The history's replacement still beside it: the re-key never reached
         * its last rename. */
        if (pending)
            write_file (history_replacement, "new history");
        write_file (image_aside, "old image");
        write_file (other_replacement, "its replacement");

        g_paste_file_backend_settle_rekeys ();

        g_assert_false (has_rekey_leftovers (names[i]));
        if (pending)
            assert_contents (image, "old image");
        else
            g_assert_false (g_file_test (image, G_FILE_TEST_EXISTS));
        g_assert_false (g_file_test (other, G_FILE_TEST_EXISTS));
    }
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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*did not finish*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*larger than anything GPaste stores*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*did not finish; not writing over it*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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

        G_PASTE_TEST_TRAP_SUBPROCESS ("*Could not list the images*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
    g_autoptr (GError) error = NULL;

    prompt->passphrase = "two";
    g_atomic_int_set (&derivations, 0);
    g_assert_true (run_rekey (settings, prompt, &error));
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

    if (!library_calls_interposed ())
        return;

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
        g_autoptr (GError) error = NULL;

        g_paste_settings_set_storage_backend (settings, (i) ? G_PASTE_STORAGE_ENCRYPTED_FILE : G_PASTE_STORAGE_FILE);
        g_paste_storage_backend_set_passphrase ((i) ? "set" : NULL);
        g_assert_true (run_decryption (settings, prompt, &error));
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
        g_autoptr (GError) error = NULL;

        if (i)
        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "typed once");
            g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("kept"));

            g_paste_storage_backend_write_history (backend, name, items);
            g_paste_storage_backend_set_passphrase (NULL);
        }

        g_assert_true (run_decryption (settings, prompt, &error));
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

    /* A whole frame under a header asking for more work than any key is
     * derived for: the operations limit, past the magic and the salt. */
    g_autofree gchar *unreasonable = g_paste_util_get_history_file_path ("unreasonable", "xmls");
    g_autofree gchar *demanding = g_memdup2 (stream, stream_length);

    memset (demanding + 8 + crypto_pwhash_SALTBYTES, 0xff, 8);
    g_assert_true (g_file_set_contents (unreasonable, demanding, stream_length, NULL));
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

        /* Items, and no parameters to check a key against. */
        g_autofree gchar *unkeyed = g_paste_util_get_history_file_path ("unkeyed", "dbs");
        g_autoptr (sqlite3) db = NULL;

        g_paste_storage_backend_write_history (backend, "unkeyed", items);
        g_assert_cmpint (sqlite3_open (unkeyed, &db), ==, SQLITE_OK);
        g_assert_cmpint (sqlite3_exec (db, "DELETE FROM meta;", NULL, NULL, NULL), ==, SQLITE_OK);
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
    if (!library_calls_interposed ())
        return;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GError) error = NULL;

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    hidden_kind = G_PASTE_STORAGE_ENCRYPTED_FILE;
    prompt->passphrase = "anything";
    g_assert_false (run_rekey (settings, prompt, &error));
    hidden_kind = -1;

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

    G_PASTE_TEST_IN_SUBPROCESS;

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
    g_assert_true (journal_write (JOURNAL_REKEY, G_PASTE_STORAGE_ENCRYPTED_FILE, journaled, NULL, NULL));
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
    g_paste_storage_rekey_async (G_PASTE_PROMPT (prompt), settings, g_paste_test_env_keep_result, &result);
    /* The worker starts and reports in two turns of the main loop, so it is
     * seen running between them. */
    g_paste_test_bus_wait_until (busy_or_answered, &result, NULL);
    g_assert_true (g_paste_storage_is_busy ());
    g_paste_test_env_wait_result_within (&result, G_PASTE_TEST_STORAGE_WAIT_SECONDS);
    g_assert_false (g_paste_storage_is_busy ());
    g_assert_true (g_paste_storage_rekey_finish (result, &error));
    g_assert_no_error (error);
    g_paste_storage_backend_set_passphrase (NULL);
}

static void
write_under (GPasteSettings *settings,
             const gchar    *passphrase,
             const gchar    *name)
{
    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, passphrase);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new (name));

    g_paste_storage_backend_write_history (backend, name, items);
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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*change of passphrase was cut short: “other” is on the new passphrase, the other*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    g_autofree gchar *journal = journal_path ();
    g_autoptr (GPtrArray) journaled = g_ptr_array_new ();

    write_under (settings, "one", name);
    write_under (settings, "one", "other");
    g_ptr_array_add (journaled, "other");
    g_assert_true (journal_write (JOURNAL_REKEY, G_PASTE_STORAGE_ENCRYPTED_FILE, journaled, NULL, NULL));
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_paste_storage_backend_set_passphrase ("one");

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GError) error = NULL;

    /* Kept until reported: a process ending first leaves it for the next start
     * to report. */
    g_assert_true (g_paste_storage_decryption_needed (settings));
    g_assert_true (g_file_test (journal, G_FILE_TEST_EXISTS));
    g_assert_true (run_decryption (settings, prompt, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (prompt->reports, ==, 1);
    g_assert_nonnull (strstr (prompt->message, "“other” are on the new passphrase"));
    g_assert_false (g_file_test (journal, G_FILE_TEST_EXISTS));
    g_assert_cmpuint (prompt->passphrase_asks, ==, 0);
    g_assert_false (g_paste_storage_decryption_needed (settings));

    /* Each history's re-key begins with itself recorded as under way, the
     * second's with the first recorded as done. */
    prompt->passphrase = "two";
    rekey_journals = g_ptr_array_new_with_free_func (g_free);
    g_assert_true (run_rekey (settings, prompt, &error));
    g_assert_false (g_file_test (journal, G_FILE_TEST_EXISTS));
    g_assert_cmpuint (rekey_journals->len, ==, 2);

    g_auto (GStrv) first = g_strsplit (g_ptr_array_index (rekey_journals, 0), "\n", -1);
    g_auto (GStrv) second = g_strsplit (g_ptr_array_index (rekey_journals, 1), "\n", -1);

    g_assert_cmpuint (g_strv_length (first), ==, 3);
    g_assert_true (g_str_has_prefix (first[0], "rekey "));
    g_assert_true (g_str_has_prefix (first[1], "/"));
    g_assert_cmpuint (g_strv_length (second), ==, 4);
    g_assert_cmpstr (second[1], ==, first[1] + 1);
    g_assert_true (g_str_has_prefix (second[2], "/"));
    g_clear_pointer (&rekey_journals, g_ptr_array_unref);
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
    g_paste_storage_backend_set_passphrase (NULL);
}

/* A process dying in a history's re-key, past what makes it the new
 * passphrase's but before its journal says so, leaves that history named as
 * under way: the next start reports it as on either passphrase rather than
 * saying nothing. */
static void
test_rekey_cut_short_under_way (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*change of passphrase was cut short: “*” on either, the other*");
        /* None is on the new passphrase, and the warning names none. */
        g_test_trap_assert_stderr_unmatched ("*“”*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    g_autoptr (GPtrArray) none = g_ptr_array_new ();

    write_under (settings, "one", name);

    g_assert_true (journal_write (JOURNAL_REKEY, G_PASTE_STORAGE_ENCRYPTED_FILE, none, name, NULL));
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_paste_storage_backend_set_passphrase ("one");

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GError) error = NULL;
    g_autofree gchar *quoted = g_strdup_printf ("“%s” was being changed", name);

    g_assert_true (g_paste_storage_decryption_needed (settings));
    g_assert_true (run_decryption (settings, prompt, &error));
    g_assert_cmpuint (prompt->reports, ==, 1);
    g_assert_nonnull (strstr (prompt->message, quoted));
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
    g_paste_storage_backend_set_passphrase (NULL);
}

/* A change of passphrase cut short is told before a migration in the same
 * start asks anything, its journal going then: kept for the decryption gate
 * after it, the journal would refuse the import as copies left to take back. */
static void
test_rekey_cut_short_then_import (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*change of passphrase was cut short*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    g_autoptr (GPtrArray) none = g_ptr_array_new ();
    g_autofree gchar *journal = journal_path ();

    write_under (settings, "one", name);

    g_assert_true (journal_write (JOURNAL_REKEY, G_PASTE_STORAGE_ENCRYPTED_FILE, none, name, NULL));
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_paste_settings_set_storage_backend_revision (settings, G_PASTE_STORAGE_BACKEND_REVISION - 1);
    g_paste_storage_backend_set_passphrase ("one");

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GError) error = NULL;
    g_autofree gchar *quoted = g_strdup_printf ("“%s” was being changed", name);

    prompt->chosen = G_PASTE_STORAGE_FILE;
    prompt->import = TRUE;
    g_assert_true (run_migration (settings, prompt, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (prompt->reports, ==, 1);
    g_assert_nonnull (strstr (prompt->message, quoted));
    g_assert_cmpuint (prompt->migrations, ==, 1);
    g_assert_cmpuint (prompt->migrations_at_report, ==, 0);
    g_assert_false (g_file_test (journal, G_FILE_TEST_EXISTS));
    g_assert_cmpint (g_paste_settings_get_storage_backend (settings), ==, G_PASTE_STORAGE_FILE);

    g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autolist (GPasteItem) imported = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (file, name, NULL, &imported, &size));
    g_assert_cmpuint (g_list_length (imported), ==, 1);
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
    g_paste_storage_backend_set_passphrase (NULL);
}

/* A change of passphrase's journal naming no history -- what its rollback
 * leaves, every history put back, a process dying before the journal goes --
 * reports no split: no history is on the new passphrase. */
static void
test_rekey_journal_naming_none (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPtrArray) none = g_ptr_array_new ();
    g_autofree gchar *journal = journal_path ();

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);
    g_assert_true (journal_write (JOURNAL_REKEY, G_PASTE_STORAGE_ENCRYPTED_FILE, none, NULL, NULL));
    g_assert_false (g_paste_storage_decryption_needed (settings));
    g_assert_false (g_file_test (journal, G_FILE_TEST_EXISTS));
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
}

/* A change of passphrase that fails in the process puts back what it did and
 * says so itself: it leaves no journal for the next start to report again. */
static void
test_rekey_failed_leaves_no_journal (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*injected failure moving*");
        g_test_trap_assert_stderr_unmatched ("*left on the new passphrase*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
    g_autoptr (GError) error = NULL;

    prompt->passphrase = "two";
    rekey_journals = g_ptr_array_new_with_free_func (g_free);
    failed_rekey = 2;
    g_assert_false (run_rekey (settings, prompt, &error));
    failed_rekey = 0;
    failed_moves = NULL;

    /* The two re-keys, then the first put back. */
    g_assert_cmpuint (rekey_journals->len, ==, 3);
    g_assert_true (g_str_has_prefix (g_ptr_array_index (rekey_journals, 1), "rekey "));
    g_assert_false (g_file_test (journal, G_FILE_TEST_EXISTS));
    g_clear_pointer (&rekey_journals, g_ptr_array_unref);

    /* Both on the passphrase the change was refused away from. */
    g_autoptr (GPasteStorageBackend) one = g_paste_file_backend_new_encrypted (settings, "one");
    const gchar *names[] = { name, "other" };

    for (gsize i = 0; i < G_N_ELEMENTS (names); ++i)
    {
        g_autolist (GPasteItem) read = NULL;
        gsize size = 0;

        g_assert_true (g_paste_storage_backend_read_history (one, names[i], NULL, &read, &size));
        g_assert_cmpuint (g_list_length (read), ==, 1);
    }
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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*is on another passphrase, which it keeps*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
    g_autoptr (GError) error = NULL;

    prompt->passphrase = "three";
    g_assert_true (run_rekey (settings, prompt, &error));
    g_assert_no_error (error);
    /* Nothing written for the history left on its passphrase either: asked
     * whether it is under the new one already, it says it is not before it is
     * copied as its replacement. */
    g_assert_cmpuint (rekey_copies, ==, 0);

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

/* A change of passphrase finding every history on another passphrase than the
 * one in use -- the history it opened deleted since -- says so, rather than
 * that there is no history to change, and changes none: the user is told how
 * to put them all on one, not that the history keeps the one it had. */
static void
test_rekey_all_elsewhere (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*is on another passphrase, which it keeps*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    write_under (settings, "two", "elsewhere");

    g_autoptr (GPastePassphrase) current = g_paste_passphrase_new ("one");
    RekeyData self = {
        .settings = settings,
        .storage_kind = G_PASTE_STORAGE_ENCRYPTED_FILE,
        .current_passphrase = current,
    };
    g_autoptr (GError) error = NULL;

    g_assert_false (rekey_histories (&self, "three", &error));
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    g_assert_nonnull (strstr (error->message, "another passphrase"));
    g_assert_false (self.split);
    g_assert_cmpint (rekey_failure_description (&self), ==, G_PASTE_PROMPT_TEXT_REKEY_ELSEWHERE_DESCRIPTION);

    g_autoptr (GPasteStorageBackend) two = g_paste_file_backend_new_encrypted (settings, "two");
    g_autolist (GPasteItem) kept = NULL;
    gsize size;

    g_assert_true (g_paste_storage_backend_read_history (two, "elsewhere", NULL, &kept, &size));
    g_assert_cmpuint (g_list_length (kept), ==, 1);
}

/* What the user is told about a re-key that left the history split is that it
 * did, not that the history still takes the passphrase it had. */
static void
test_rekey_split_reported (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*left partly on the new passphrase*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    g_autofree gchar *history = NULL;
    g_autofree gchar *image = NULL;

    write_encrypted_history (settings, name, "old passphrase", &history, &image);
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_paste_storage_backend_set_passphrase ("old passphrase");

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    g_autoptr (GError) error = NULL;
    const gchar * const no_way_back[] = { ".xmls.rekey", ".pngs.rekey-old", NULL };

    prompt->passphrase = "new passphrase";
    failed_moves = no_way_back;
    g_assert_false (run_rekey (settings, prompt, &error));
    failed_moves = NULL;

    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    g_assert_cmpuint (prompt->reports, ==, 1);
    g_assert_true (g_str_has_prefix (prompt->message, g_paste_prompt_text (G_PASTE_PROMPT_TEXT_REKEY_SPLIT_DESCRIPTION)));
}
#endif

/* Check the store through @prompt, handing back its report. */
static GStrv
run_doctor (GPasteSettings      *settings,
            TestMigrationPrompt *prompt,
            gboolean            *healthy)
{
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;
    GStrv report = NULL;

    g_paste_storage_doctor_async (G_PASTE_PROMPT (prompt), settings, g_paste_test_env_keep_result, &result);
    g_paste_test_env_wait_result_within (&result, G_PASTE_TEST_STORAGE_WAIT_SECONDS);
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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Failed to install the re-encrypted*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*failed to decrypt an item*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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
                                           "Wrong for \xff", g_paste_test_env_keep_result, &result);
    g_paste_test_env_wait_result_within (&result, G_PASTE_TEST_BUS_WAIT_SECONDS);
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
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Failed to parse history file*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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

/* Repaired as well when the history in use holds nothing to go by -- a new
 * one, or the default one a deletion left the user on: the passphrase in hand
 * is one a history confirms, and the check goes on with it rather than refuse
 * it, and the other one with it, over the histories each does not open. */
static void
test_doctor_split_empty_active (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    write_under (settings, "one", "work");
    write_under (settings, "two", "personal");
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_paste_storage_backend_set_passphrase ("one");

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    const gchar * const typed[] = { "two", NULL };
    gboolean healthy = FALSE;

    prompt->passphrases = typed;

    g_auto (GStrv) report = run_doctor (settings, prompt, &healthy);

    g_assert_true (healthy);
    g_assert_cmpuint (prompt->passphrase_asks, ==, 1);
    g_assert_nonnull (strstr (prompt->passphrase_description, "personal"));
    g_assert_true (opens_under (settings, "one", "work"));
    g_assert_true (opens_under (settings, "one", "personal"));
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

/* A history whose document opens with the passphrase in use, its image left
 * on another one, as a change of passphrase cut short leaves it: the image's
 * side file. */
static gchar *
write_split_by_images (GPasteSettings *settings,
                       const gchar    *name)
{
    g_autofree gchar *history = NULL;
    gchar *image = NULL;
    g_autofree gchar *elsewhere_history = NULL;
    g_autofree gchar *elsewhere_image = NULL;

    write_encrypted_history (settings, "images-elsewhere", "two", &elsewhere_history, &elsewhere_image);

    g_autoptr (GBytes) on_two = read_bytes (elsewhere_image);

    {
        g_autoptr (GPasteStorageBackend) two = g_paste_file_backend_new_encrypted (settings, "two");

        g_paste_storage_backend_delete_history (two, "images-elsewhere", NULL);
    }
    write_encrypted_history (settings, name, "one", &history, &image);
    write_bytes (image, on_two);

    return image;
}

/* Whether @name opens under @passphrase with its image whole. */
static gboolean
opens_with_image (GPasteSettings *settings,
                  const gchar    *passphrase,
                  const gchar    *name)
{
    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, passphrase);
    g_autolist (GPasteItem) items = NULL;
    gsize size = 0;

    return g_paste_storage_backend_read_history (backend, name, NULL, &items, &size) && g_list_length (items) == 2 &&
           g_paste_image_item_get_png_bytes (G_PASTE_IMAGE_ITEM (items->next->data));
}

/* A history whose document opens with the passphrase in use but whose images
 * are on another one is no healthy one either, the read dropping them and the
 * next full write sweeping them: the other passphrase is asked for, naming
 * it, and the images are encrypted with the one in use, the document left as
 * it is. Dismissed, they are said to be unreadable. */
static void
test_doctor_split_images (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Failed to load image*");
        return;
    }

    /* The image's read warns, which is the case itself; a critical still
     * fails it. */
    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);

    g_autofree gchar *image = write_split_by_images (settings, name);

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_paste_storage_backend_set_passphrase ("one");
    g_assert_false (opens_with_image (settings, "one", name));

    /* Dismissed, the image is offered to be deleted, and kept when that is
     * declined too. */
    {
        g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
        gboolean healthy = TRUE;
        g_auto (GStrv) report = run_doctor (settings, prompt, &healthy);

        g_assert_false (healthy);
        g_assert_cmpuint (prompt->passphrase_asks, ==, 1);
        g_assert_cmpuint (prompt->confirms, ==, 1);
        g_assert_nonnull (strstr (prompt->confirm_description, name));
        g_assert_cmpuint (g_strv_length (report), ==, 1);
        g_assert_nonnull (strstr (report[0], "Images of the history"));
        g_assert_true (g_file_test (image, G_FILE_TEST_EXISTS));
    }

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    const gchar * const typed[] = { "two", NULL };
    gboolean healthy = FALSE;

    prompt->passphrases = typed;

    g_auto (GStrv) report = run_doctor (settings, prompt, &healthy);

    g_assert_true (healthy);
    g_assert_cmpuint (prompt->passphrase_asks, ==, 1);
    g_assert_cmpuint (prompt->confirms, ==, 0);
    g_assert_nonnull (strstr (prompt->passphrase_description, name));
    g_assert_true (opens_with_image (settings, "one", name));
    g_paste_storage_backend_set_passphrase (NULL);
}

/* The document of a history split by its images goes through the re-key as it
 * is, copied as its replacement, the new key asked of it once: the copy is read
 * back by its bytes, one that does not having the re-key fail with nothing
 * moved, rather than put in place a document no key reads. */
static void
test_rekey_document_carried_through (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*does not read back*");
        return;
    }

    /* As in test_doctor_split_images (). */
    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "document-carried-through";
    g_autofree gchar *image = write_split_by_images (settings, name);
    g_autoptr (GPasteStorageBackend) two = g_paste_file_backend_new_encrypted (settings, "two");
    g_autofree gchar *document = g_paste_storage_backend_get_history_file_path (two, name);
    g_autofree gchar *replacement = g_strconcat (document, G_PASTE_FILE_BACKEND_REKEY_NEW, NULL);
    g_autoptr (GBytes) before = read_bytes (document);
    gboolean split = FALSE;

    rekey_copies_damaged = TRUE;
    g_assert_false (g_paste_storage_backend_rekey (two, name, "one", &split));
    rekey_copies_damaged = FALSE;
    g_assert_false (split);
    g_assert_false (g_file_test (replacement, G_FILE_TEST_EXISTS));

    {
        g_autoptr (GBytes) kept = read_bytes (document);

        g_assert_true (g_bytes_equal (before, kept));
        g_assert_true (g_file_test (image, G_FILE_TEST_EXISTS));
    }

    g_atomic_int_set (&derivations, 0);
    g_assert_true (g_paste_storage_backend_rekey (two, name, "one", &split));
    /* The document refusing the old key and asked of the new one, then the
     * image read with the old key, and written and read back with the new. */
    g_assert_cmpint (g_atomic_int_get (&derivations), ==, 2 + 3);

    g_autoptr (GBytes) after = read_bytes (document);

    g_assert_true (g_bytes_equal (before, after));
    g_assert_true (opens_with_image (settings, "one", name));
}

/* An image no passphrase typed opens -- damaged, as no passphrase opens it --
 * is asked about once a check rather than until the prompt is dismissed, and
 * deleted once that is confirmed: the check after finds nothing wrong. */
static void
test_doctor_split_images_unreadable (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Failed to load image*");
        return;
    }

    /* As in test_doctor_split_images (). */
    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    g_autofree gchar *image = write_split_by_images (settings, name);
    const gchar * const typed[] = { "three", "two", NULL };

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_paste_storage_backend_set_passphrase ("one");

    {
        g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
        gboolean healthy = TRUE;

        prompt->passphrases = typed;

        g_auto (GStrv) report = run_doctor (settings, prompt, &healthy);

        g_assert_false (healthy);
        g_assert_cmpuint (prompt->passphrase_asks, ==, 1);
        g_assert_cmpuint (prompt->confirms, ==, 1);
        g_assert_cmpuint (g_strv_length (report), ==, 1);
        g_assert_nonnull (strstr (report[0], "Images of the history"));
        g_assert_true (g_file_test (image, G_FILE_TEST_EXISTS));
    }

    {
        g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
        gboolean healthy = FALSE;

        prompt->passphrases = typed;
        prompt->confirm = TRUE;

        g_auto (GStrv) report = run_doctor (settings, prompt, &healthy);

        g_assert_true (healthy);
        g_assert_cmpuint (prompt->confirms, ==, 1);
        g_assert_cmpuint (g_strv_length (report), ==, 1);
        g_assert_nonnull (strstr (report[0], "Deleted the unreadable images"));
        g_assert_false (g_file_test (image, G_FILE_TEST_EXISTS));
    }

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);
    gboolean healthy = FALSE;
    g_auto (GStrv) report = run_doctor (settings, prompt, &healthy);

    g_assert_true (healthy);
    g_assert_cmpuint (prompt->passphrase_asks, ==, 0);
    g_assert_cmpuint (prompt->confirms, ==, 0);
    g_paste_storage_backend_set_passphrase (NULL);
}

/* What a change of passphrase the daemon taken over from did not live to
 * finish leaves is settled once that daemon lets go of the store, by the
 * decryption gate as by the migration one, though nothing is left to unlock:
 * the history it left half re-keyed would refuse every read for the session. */
static void
test_decryption_gate_settles (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    g_autofree gchar *history = g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_ENCRYPTED_FILE));
    g_autofree gchar *aside = g_strconcat (history, ".rekey-old", NULL);
    g_autofree gchar *replacement = g_strconcat (history, ".rekey", NULL);
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    /* A re-key cut short before its last rename: the original under its
     * second name too, and the replacement beside it. */
    write_under (settings, "passphrase", name);
    g_assert_cmpint (link (history, aside), ==, 0);
    write_file (replacement, "half re-keyed");

    gint fd = hold_storage_lock ();

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_paste_storage_backend_set_passphrase ("passphrase");
    g_assert_true (g_paste_storage_decryption_needed (settings));
    g_assert_true (has_rekey_leftovers (name));

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);

    g_paste_storage_decryption_async (G_PASTE_PROMPT (prompt), settings, g_paste_test_env_keep_result, &result);
    g_paste_test_bus_pump (300);
    g_assert_null (result);
    close (fd);
    g_paste_test_env_wait_result_within (&result, G_PASTE_TEST_STORAGE_WAIT_SECONDS);
    g_assert_true (g_paste_storage_decryption_finish (result, &error));
    g_assert_no_error (error);
    g_assert_false (has_rekey_leftovers (name));
    g_assert_true (opens_under (settings, "passphrase", name));
    g_assert_cmpuint (prompt->passphrase_asks, ==, 0);
    g_paste_storage_backend_set_passphrase (NULL);
}

#ifdef G_PASTE_ENABLE_LIBSECRET
static guint keyring_lookups;

/* Counts the warning a keyring lookup gives on the test bus, which has none. */
static GLogWriterOutput
count_keyring_lookups (GLogLevelFlags   log_level,
                       const GLogField *fields,
                       gsize            n_fields,
                       gpointer         user_data)
{
    for (gsize i = 0; i < n_fields; ++i)
    {
        if (g_paste_str_equal (fields[i].key, "MESSAGE") && fields[i].length < 0 &&
            strstr (fields[i].value, "look up the history passphrase in the keyring"))
            ++keyring_lookups;
    }

    return g_log_writer_default (log_level, fields, n_fields, user_data);
}

/* The keyring is asked once the store is this process's, which the
 * synchronous check leaves to the gate while another daemon holds it, and
 * before any prompt. */
static void
test_decryption_gate_asks_keyring (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_log_set_writer_func (count_keyring_lookups, NULL, NULL);
    G_PASTE_TEST_IN_SUBPROCESS;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = g_paste_settings_get_history_name (settings);
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    write_under (settings, "passphrase", name);

    gint fd = hold_storage_lock ();

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_assert_true (g_paste_storage_decryption_needed (settings));

    g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);

    prompt->passphrase = "passphrase";
    g_paste_storage_decryption_async (G_PASTE_PROMPT (prompt), settings, g_paste_test_env_keep_result, &result);
    g_paste_test_bus_pump (300);
    g_assert_cmpuint (keyring_lookups, ==, 0);
    close (fd);
    g_paste_test_env_wait_result_within (&result, G_PASTE_TEST_STORAGE_WAIT_SECONDS);
    g_assert_true (g_paste_storage_decryption_finish (result, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (keyring_lookups, ==, 1);
    g_assert_cmpuint (prompt->passphrase_asks, ==, 1);
    g_paste_storage_backend_set_passphrase (NULL);
}
#endif

/* A locked history is unlocked first; left locked, what it holds is not
 * checked and the store is not called healthy. */
static void
test_doctor_locked (void)
{
    /* The keyring is looked up first, which the test bus has none of. */
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*Could not look up the history passphrase in the keyring*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

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

typedef struct
{
    guint runs;
    guint freed;
} IdleWait;

static void
count_idle_run (gpointer data)
{
    ++((IdleWait *) data)->runs;
}

static void
count_idle_free (gpointer data)
{
    ++((IdleWait *) data)->freed;
}

static void
iterate (void)
{
    for (guint i = 0; i < 5; ++i)
        g_main_context_iteration (NULL, FALSE);
}

/* A wait for the store to be idle runs once, from the main context, the moment
 * the last worker has reported and not before -- nor at once when none runs,
 * but from the next iteration; given up on, it never does. Its data goes
 * either way. A worker is stood in for by the count it keeps. */
static void
test_storage_when_idle (void)
{
    IdleWait busy = { 0 };
    IdleWait dropped = { 0 };
    IdleWait idle = { 0 };

    ++g_paste_storage_workers;
    g_paste_storage_when_idle (count_idle_run, &busy, count_idle_free);
    iterate ();
    g_assert_cmpuint (busy.runs, ==, 0);
    --g_paste_storage_workers;
    iterate ();
    g_assert_cmpuint (busy.runs, ==, 1);
    g_assert_cmpuint (busy.freed, ==, 1);

    g_source_remove (g_paste_storage_when_idle (count_idle_run, &dropped, count_idle_free));
    iterate ();
    g_assert_cmpuint (dropped.runs, ==, 0);
    g_assert_cmpuint (dropped.freed, ==, 1);

    g_paste_storage_when_idle (count_idle_run, &idle, count_idle_free);
    g_assert_cmpuint (idle.runs, ==, 0);
    iterate ();
    g_assert_cmpuint (idle.runs, ==, 1);
    g_assert_cmpuint (idle.freed, ==, 1);
}

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
    g_test_add_func ("/storage/journal/empty", test_journal_empty);
    g_test_add_func ("/storage/journal/unwritable", test_journal_unwritable);
    g_test_add_func ("/storage/fifo-history", test_fifo_history);
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
    g_test_add_func ("/storage/lock/gates", test_gates_wait_for_lock);
    g_test_add_data_func ("/storage/lock/gate-released-before-wait", GINT_TO_POINTER (FALSE), test_gate_released_before_wait);
    g_test_add_data_func ("/storage/lock/gate-released-then-relocked", GINT_TO_POINTER (TRUE), test_gate_released_before_wait);
    g_test_add_func ("/storage/lock/gate-released-after-wait", test_gate_released_after_wait);
    g_test_add_func ("/storage/lock/released-then-relocked-while-waiting", test_lock_released_then_relocked_while_waiting);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_test_add_func ("/storage/lock/decryption-gate-settles", test_decryption_gate_settles);
#ifdef G_PASTE_ENABLE_LIBSECRET
    g_test_add_func ("/storage/lock/decryption-gate-asks-keyring", test_decryption_gate_asks_keyring);
#endif
#endif
    g_test_add_func ("/storage/when-idle", test_storage_when_idle);
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
    g_test_add_func ("/storage/unstored-leftovers-swept", test_unstored_leftovers_swept);
    g_test_add_func ("/storage/new-history-write-failed", test_new_history_write_failed);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_test_add_func ("/storage/encrypted-synced-before-install", test_encrypted_synced_before_install);
#endif
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_test_add_func ("/storage/passphrase-survives-damage", test_passphrase_survives_damage);
    g_test_add_func ("/storage/passphrase/verify-cost", test_passphrase_verify_cost);
    g_test_add_func ("/storage/passphrase/verify-underived", test_passphrase_verify_underived);
    g_test_add_func ("/storage/passphrase/set-again", test_passphrase_set_again);
    g_test_add_func ("/storage/passphrase/keyed-backend-reads-none", test_keyed_backend_reads_no_process_passphrase);
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
    g_test_add_func ("/storage/rekey/copy-cut-short", test_rekey_copy_cut_short);
    g_test_add_func ("/storage/rekey/past-unreadable-image", test_rekey_past_unreadable_image);
    g_test_add_func ("/storage/rekey/split-reported", test_rekey_split_reported);
    g_test_add_func ("/storage/rekey/settled-back", test_rekey_settled_back);
    g_test_add_func ("/storage/rekey/settle-unlisted", test_rekey_settle_unlisted);
    g_test_add_func ("/storage/rekey/settle-lists-once", test_rekey_settle_lists_once);
    g_test_add_func ("/storage/failed-write-no-temporary", test_failed_write_leaves_no_temporary);
    g_test_add_func ("/storage/rekey/settled-forward", test_rekey_settled_forward);
    g_test_add_func ("/storage/rekey/settled-without-image", test_rekey_settled_without_image);
    g_test_add_func ("/storage/rekey/unsettled-refused", test_rekey_unsettled_refused);
    g_test_add_func ("/storage/rekey/leftovers-deleted", test_rekey_leftovers_deleted);
    g_test_add_func ("/storage/rekey/marker-kept-by-failed-delete", test_rekey_marker_kept_by_failed_delete);
    g_test_add_func ("/storage/rekey/unsettled-unwritten", test_rekey_unsettled_unwritten);
    g_test_add_func ("/storage/encrypted-read-oversized", test_encrypted_read_oversized);
    g_test_add_func ("/storage/rekey/settled-without-history", test_rekey_settled_without_history);
    g_test_add_func ("/storage/rekey/split-passphrases", test_rekey_split_passphrases);
    g_test_add_func ("/storage/rekey/all-elsewhere", test_rekey_all_elsewhere);
    g_test_add_func ("/storage/rekey/cost", test_rekey_cost);
    g_test_add_func ("/storage/rekey/cut-short", test_rekey_cut_short);
    g_test_add_func ("/storage/rekey/failed-leaves-no-journal", test_rekey_failed_leaves_no_journal);
    g_test_add_func ("/storage/rekey/cut-short-under-way", test_rekey_cut_short_under_way);
    g_test_add_func ("/storage/rekey/cut-short-then-import", test_rekey_cut_short_then_import);
    g_test_add_func ("/storage/rekey/journal-naming-none", test_rekey_journal_naming_none);
    g_test_add_func ("/storage/doctor/split-repaired", test_doctor_split_repaired);
    g_test_add_func ("/storage/doctor/split-empty-active", test_doctor_split_empty_active);
    g_test_add_func ("/storage/doctor/split-dismissed", test_doctor_split_dismissed);
    g_test_add_func ("/storage/doctor/split-images", test_doctor_split_images);
    g_test_add_func ("/storage/rekey/document-carried-through", test_rekey_document_carried_through);
    g_test_add_func ("/storage/doctor/split-images-unreadable", test_doctor_split_images_unreadable);
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
    g_test_add_func ("/storage/secret-search-counts", test_secret_search_counts);
    g_test_add_func ("/storage/encrypted-read-leaves-no-copy", test_encrypted_read_leaves_no_copy);
    g_test_add_func ("/storage/encrypted-failed-read-leaves-no-copy", test_encrypted_failed_read_leaves_no_copy);
#endif
#ifdef G_PASTE_ENABLE_SQLITE
    g_test_add_data_func ("/storage/sqlite/schema/alter", "ALTER TABLE items ADD COLUMN timeout", test_schema_failure);
    g_test_add_data_func ("/storage/sqlite/schema/version", "PRAGMA user_version = 3", test_schema_failure);
    g_test_add_data_func ("/storage/sqlite/schema/commit", "COMMIT;", test_schema_failure);
    g_test_add_func ("/storage/sqlite/checksum-refused", test_sqlite_checksum_refused);
    g_test_add_func ("/storage/sqlite/schema/unreadable-version", test_schema_version_unreadable);
    g_test_add_func ("/storage/sqlite/schema/upgraded-meanwhile", test_schema_upgraded_meanwhile);
    g_test_add_func ("/storage/sqlite/eviction-checkpoints-nothing", test_sqlite_eviction_checkpoints_nothing);
    g_test_add_func ("/storage/sqlite/checkpoint-spares-readers", test_sqlite_checkpoint_spares_readers);
    g_test_add_func ("/storage/sqlite/left-out-accounted", test_sqlite_left_out_accounted);
    g_test_add_func ("/storage/sqlite/select-moves-only", test_sqlite_select_moves_only);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_test_add_func ("/storage/sqlite/rekey-checkpoints", test_sqlite_rekey_checkpoints);
    g_test_add_func ("/storage/sqlite/keyed-meanwhile", test_sqlite_keyed_meanwhile);
#endif
#endif
#if defined(G_PASTE_ENABLE_ENCRYPTION) && defined(G_PASTE_ENABLE_SQLITE)
    g_test_add_func ("/storage/sqlite/incomplete-crypto", test_incomplete_crypto);
    g_test_add_func ("/storage/sqlite/uncounted-crypto", test_uncounted_crypto);
    g_test_add_func ("/storage/sqlite/busy-verification", test_verification_waits_for_busy_database);
    g_test_add_func ("/storage/sqlite/verification-empty-active-database", test_verification_empty_active_database);
#endif
    g_test_add_func ("/storage/migration/offered", test_migration_offers_supported);
    g_test_add_func ("/storage/list-histories-off-main-loop", test_list_histories_counted_off_main_loop);
    g_test_add_func ("/storage/list-histories-current-as-answered", test_list_histories_current_as_answered);
    g_test_add_func ("/storage/list-histories-recounted-after-empty", test_list_histories_recounted_after_empty);
    g_test_add_func ("/storage/list-histories-recounted-after-leave", test_list_histories_recounted_after_leave);
    g_test_add_func ("/storage/list-histories-history-dropped", test_list_histories_history_dropped);
    g_test_add_func ("/storage/name-fits-directory", test_name_fits_directory);
    g_test_add_func ("/storage/delete-unsupported-refused", test_delete_unsupported_refused);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_test_add_func ("/storage/encrypted-stream-error-after-progress", test_stream_error_after_progress);
    g_test_add_func ("/storage/has-frame-reads-one-file", test_has_frame_reads_one_file);
#endif
    g_test_add_func ("/storage/cancelled-read-creates-nothing", test_cancelled_read_creates_nothing);
    g_test_add_func ("/storage/file-diverged-history", test_file_diverged_history);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_test_add_func ("/storage/passphrase/damaged-active-asked-once", test_passphrase_damaged_active_asked_once);
#endif
#ifdef G_PASTE_ENABLE_SQLITE
    g_test_add_func ("/storage/migration/unopenable-source", test_migration_clamps_unopenable_source);
    g_test_add_func ("/storage/unopenable-out-of-reach", test_unopenable_store_out_of_reach);
    g_test_add_func ("/storage/migration/unoffered-choice", test_migration_refuses_unoffered_choice);
    g_test_add_func ("/storage/migration/import-failure", test_import_failure_leaves_no_copy);
    g_test_add_func ("/storage/migration/import-readability", test_import_needs_readable_copy);
    g_test_add_func ("/storage/migration/import-copies-whole", test_import_copies_whole);
    g_test_add_func ("/storage/migration/import-cut-short", test_import_cut_short);
    g_test_add_func ("/storage/migration/import-cut-short-then-migrate", test_import_cut_short_then_migrate);
    g_test_add_func ("/storage/migration/import-busy", test_import_busy);
    g_test_add_func ("/storage/sqlite/delete-during-read", test_sqlite_delete_during_read);
    g_test_add_func ("/storage/sqlite/forgotten-spares-new-log", test_sqlite_forgotten_spares_new_log);
    g_test_add_func ("/storage/migration/import-spares-unreadable-destination", test_import_spares_unreadable_destination);
    g_test_add_func ("/storage/migration/import-spares-unwritten-destination", test_import_spares_unwritten_destination);
    g_test_add_func ("/storage/migration/import-takes-back-partial-destination", test_import_takes_back_partial_destination);
    g_test_add_func ("/storage/migration/import-death-spares-kept-copy", test_import_death_spares_kept_copy);
    g_test_add_func ("/storage/migration/import-excuses-left-out", test_import_excuses_left_out);
    g_test_add_func ("/storage/migration/import-rollback-keeps-undeleted", test_import_rollback_keeps_undeleted);
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
    g_test_add_func ("/storage/sqlite/diverged-add-converges", test_sqlite_diverged_add_converges);
    g_test_add_func ("/storage/sqlite/unranked-add", test_sqlite_unranked_add);
    g_test_add_func ("/storage/sqlite/emptied-converges", test_sqlite_emptied_converges);
    g_test_add_func ("/storage/sqlite/item-too-large", test_sqlite_item_too_large);
#endif

    return g_paste_test_env_run ();
}
