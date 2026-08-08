// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-history-saver.h>

#include <gio/gio.h>

/* Owns the asynchronous persistence of a history: it writes snapshots handed to
 * it (coalescing concurrent requests) and loads histories in the background,
 * handing the result back through a callback. It never touches the live history
 * list, so it shares no locking with the model.
 *
 * All of its state is only ever touched on the main thread: g_paste_history_saver_record
 * / _load run there, and the GTask completion callbacks fire on the thread-default
 * context they were created on. The @owner GObject is used as the GTask source so
 * it stays alive for the whole duration of every in-flight operation. Each task also
 * holds its own ref on the saver (released in its done callback), so an owner that
 * swaps its saver out mid-flight (g_paste_history_reload_backend) cannot free one
 * whose completion callback has not fired yet. */
struct _GPasteHistorySaver
{
    GObject parent_instance;

    GPasteStorageBackend        *backend;
    gpointer                     owner; /* not ref'd: owns us, outlives us */
    GPasteHistorySaverLoadedFunc loaded;

    gboolean                     write_in_progress;
    /* Pending writes (GPasteHistorySaverWrite*), applied in order. With a
     * non-incremental backend each entry is a full rewrite, so the queue is
     * coalesced down to the latest snapshot and never holds more than one. */
    GQueue                       pending;

    /* Handshake for g_paste_history_saver_drain(): the worker thread clears
     * @worker_running and signals @drain_cond when it finishes its write, so a
     * synchronous drain on the main thread can wait for it without spinning the
     * main loop (which is what would otherwise dispatch the write-done callback). */
    GMutex                       drain_mutex;
    GCond                        drain_cond;
    gboolean                     worker_running;

    gboolean                     load_in_progress;
    guint64                      load_generation;

    /* Set by g_paste_history_saver_detach(): the owner has replaced us, so an
     * in-flight load (which keeps us alive through the task's own reference) must
     * not hand its now-stale result back to it. */
    gboolean                     detached;
};

G_PASTE_DEFINE_TYPE (HistorySaver, history_saver, G_TYPE_OBJECT)

/****************/
/* Write path   */
/****************/

typedef struct
{
    GPasteHistorySaver   *saver; /* not ref'd: the task's own ref keeps it alive */
    GPasteStorageBackend *backend;
    GPasteHistorySaveOp   op;
    gchar                *name;
    GPasteItem           *item; /* ref'd, or NULL */
    gchar                *uuid; /* or NULL */
    GList                *history;
} GPasteHistorySaverWrite;

static void
g_paste_history_saver_write_free (gpointer data)
{
    g_autofree GPasteHistorySaverWrite *d = data;
    g_clear_object (&d->backend);
    g_clear_pointer (&d->name, g_free);
    g_clear_object (&d->item);
    g_clear_pointer (&d->uuid, g_free);
    g_clear_list (&d->history, g_object_unref);
}

static void
g_paste_history_saver_do_write (const GPasteHistorySaverWrite *data)
{
    switch (data->op)
    {
    case G_PASTE_HISTORY_SAVE_ADD:
        g_paste_storage_backend_add_item (data->backend, data->name, data->item, data->history);
        break;
    case G_PASTE_HISTORY_SAVE_REMOVE:
        g_paste_storage_backend_remove_item (data->backend, data->name, data->uuid, data->history);
        break;
    case G_PASTE_HISTORY_SAVE_REPLACE:
        g_paste_storage_backend_replace_item (data->backend, data->name, data->uuid, data->item, data->history);
        break;
    case G_PASTE_HISTORY_SAVE_CLEAR:
        g_paste_storage_backend_clear_history (data->backend, data->name, data->history);
        break;
    case G_PASTE_HISTORY_SAVE_FULL:
    default:
        g_paste_storage_backend_write_history (data->backend, data->name, data->history);
        break;
    }
}

static void
g_paste_history_saver_write_task (GTask        *task,
                                  gpointer      source_object G_GNUC_UNUSED,
                                  gpointer      task_data,
                                  GCancellable *cancellable G_GNUC_UNUSED)
{
    const GPasteHistorySaverWrite *data = task_data;
    GPasteHistorySaver *self = data->saver;

    g_paste_history_saver_do_write (data);

    /* Let a concurrent g_paste_history_saver_drain() know this write is done. */
    g_mutex_lock (&self->drain_mutex);
    self->worker_running = FALSE;
    g_cond_signal (&self->drain_cond);
    g_mutex_unlock (&self->drain_mutex);

    g_task_return_boolean (task, TRUE);
}

static void g_paste_history_saver_write_done (GObject *source_object, GAsyncResult *result, gpointer user_data);

static void
g_paste_history_saver_start_write (GPasteHistorySaver *self)
{
    if (self->write_in_progress || g_queue_is_empty (&self->pending))
        return;

    self->write_in_progress = TRUE;

    /* Set before launching so a drain that runs before the worker starts still
     * sees the write as in flight and waits for it. */
    g_mutex_lock (&self->drain_mutex);
    self->worker_running = TRUE;
    g_mutex_unlock (&self->drain_mutex);

    GPasteHistorySaverWrite *data = g_queue_pop_head (&self->pending);

    /* Hold our own ref for the duration of the task: the owner keeps us alive
     * only as long as it still owns us, but g_paste_history_reload_backend()
     * swaps its saver out mid-flight, so rely on this ref (released in
     * write_done) rather than the owner to survive until the callback fires. */
    g_autoptr (GTask) task = g_task_new (self->owner, NULL, g_paste_history_saver_write_done, g_object_ref (self));
    g_task_set_static_name (task, "gpaste-history-write");
    g_task_set_task_data (task, data, g_paste_history_saver_write_free);
    g_task_run_in_thread (task, g_paste_history_saver_write_task);
}

static void
g_paste_history_saver_write_done (GObject      *source_object G_GNUC_UNUSED,
                                  GAsyncResult *result        G_GNUC_UNUSED,
                                  gpointer      user_data)
{
    g_autoptr (GPasteHistorySaver) self = user_data; /* the ref taken in start_write */

    self->write_in_progress = FALSE;

    /* More changes may have queued up while we were writing: drain the next. */
    g_paste_history_saver_start_write (self);
}

/**
 * g_paste_history_saver_record:
 * @self: a #GPasteHistorySaver
 * @op: the kind of change to persist
 * @name: the history name to write to
 * @item: (nullable): the item involved (for %G_PASTE_HISTORY_SAVE_ADD / _REPLACE)
 * @uuid: (nullable): the uuid involved (for %G_PASTE_HISTORY_SAVE_REMOVE / _REPLACE)
 * @history: (transfer full) (nullable) (element-type GPasteItem): a snapshot of
 *           the items, used as-is by a non-incremental backend and to reconcile
 *           an incremental add; %NULL for the operations that never consume it
 *
 * Persist a change to @name in the background. An incremental backend applies
 * the queued changes in order; for a non-incremental one (which rewrites the
 * whole snapshot anyway) successive records coalesce into the latest snapshot,
 * so only the most recent state ever reaches the disk.
 */
G_PASTE_VISIBLE void
g_paste_history_saver_record (GPasteHistorySaver  *self,
                              GPasteHistorySaveOp  op,
                              const gchar         *name,
                              const GPasteItem    *item,
                              const gchar         *uuid,
                              GList               *history)
{
    g_return_if_fail (_G_PASTE_IS_HISTORY_SAVER (self));

    /* A non-incremental backend ignores the granular hint and rewrites the whole
     * snapshot, so collapse any pending writes into a single full one. */
    if (!g_paste_storage_backend_is_incremental (self->backend))
    {
        g_queue_clear_full (&self->pending, g_paste_history_saver_write_free);
        op = G_PASTE_HISTORY_SAVE_FULL;
        item = NULL;
        uuid = NULL;
    }

    GPasteHistorySaverWrite *data = g_new0 (GPasteHistorySaverWrite, 1);
    data->saver = self;
    data->backend = g_object_ref (self->backend);
    data->op = op;
    data->name = g_strdup (name);
    data->item = item ? g_object_ref ((GPasteItem *) item) : NULL;
    data->uuid = g_strdup (uuid);
    data->history = history;

    g_queue_push_tail (&self->pending, data);

    g_paste_history_saver_start_write (self);
}

/**
 * g_paste_history_saver_drain:
 * @self: a #GPasteHistorySaver
 *
 * Synchronously flush every queued write to disk and wait for any in-flight one
 * to finish. Meant to be called on the main thread from an exit path (the daemon
 * losing its name to a takeover, or being told to stop), so no clipboard change
 * is lost before the successor loads the history. Records made afterwards would
 * queue again, so the caller must stop recording (see g_paste_history_flush()).
 */
G_PASTE_VISIBLE void
g_paste_history_saver_drain (GPasteHistorySaver *self)
{
    g_return_if_fail (_G_PASTE_IS_HISTORY_SAVER (self));

    /* Wait for the in-flight worker write (if any) to finish. We cannot spin the
     * main loop here (that is where the write-done callback would run), so the
     * worker signals us directly when its write completes. */
    g_mutex_lock (&self->drain_mutex);
    while (self->worker_running)
        g_cond_wait (&self->drain_cond, &self->drain_mutex);
    g_mutex_unlock (&self->drain_mutex);

    /* Apply whatever is still queued synchronously, in order. */
    while (!g_queue_is_empty (&self->pending))
    {
        GPasteHistorySaverWrite *data = g_queue_pop_head (&self->pending);

        g_paste_history_saver_do_write (data);
        g_paste_history_saver_write_free (data);
    }

    /* Deliberately *not* clearing write_in_progress: the drained task's completion
     * callback is still queued on the main context and will clear it (and pick up
     * anything recorded meanwhile). Clearing it here would let a record() made
     * after a resumed handover start a second worker while that callback then
     * starts a third — two threads rewriting the same history at once. */
}

/**
 * g_paste_history_saver_detach:
 * @self: a #GPasteHistorySaver
 *
 * Stop handing results back to the owner. An in-flight load keeps the saver alive
 * through its own reference, so a saver that has been replaced (see
 * g_paste_history_reload_backend()) would otherwise still install its stale, old-
 * backend history over the one the new saver just loaded.
 */
G_PASTE_VISIBLE void
g_paste_history_saver_detach (GPasteHistorySaver *self)
{
    g_return_if_fail (_G_PASTE_IS_HISTORY_SAVER (self));

    self->detached = TRUE;
}

/****************/
/* Load path    */
/****************/

typedef struct
{
    GPasteStorageBackend *backend;
    gchar                *name;
    guint64               generation;
    gboolean              save_after;
} GPasteHistorySaverLoadData;

static void
g_paste_history_saver_load_data_free (gpointer data)
{
    g_autofree GPasteHistorySaverLoadData *d = data;
    g_clear_object (&d->backend);
    g_clear_pointer (&d->name, g_free);
}

typedef struct
{
    GList   *history;
    gsize    size;
    /* %FALSE when the history is on disk but could not be read back (a failed
     * decryption, parse or I/O error), so the owner can refuse to persist over
     * data it never managed to load. */
    gboolean readable;
} GPasteHistorySaverLoadResult;

static void
g_paste_history_saver_load_result_free (GPasteHistorySaverLoadResult *result)
{
    g_autofree GPasteHistorySaverLoadResult *r = result;
    g_clear_list (&r->history, g_object_unref);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GPasteHistorySaverLoadResult, g_paste_history_saver_load_result_free)

static void
g_paste_history_saver_load_task (GTask        *task,
                                 gpointer      source_object G_GNUC_UNUSED,
                                 gpointer      task_data,
                                 GCancellable *cancellable G_GNUC_UNUSED)
{
    const GPasteHistorySaverLoadData *data = task_data;
    GPasteHistorySaverLoadResult *result = g_new0 (GPasteHistorySaverLoadResult, 1);

    /* First file access of the process: block until any previous daemon has
     * finished flushing and released the lock, so we never load a stale history. */
    g_paste_storage_backend_lock ();

    result->readable = g_paste_storage_backend_read_history (data->backend, data->name, &result->history, &result->size);
    g_task_return_pointer (task, result, (GDestroyNotify) g_paste_history_saver_load_result_free);
}

static void
g_paste_history_saver_load_done (GObject      *source_object G_GNUC_UNUSED,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
    g_autoptr (GPasteHistorySaver) self = user_data; /* the ref taken in _load */
    const GPasteHistorySaverLoadData *data = g_task_get_task_data (G_TASK (result));

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteHistorySaverLoadResult) load_result = g_task_propagate_pointer (G_TASK (result), &error);

    /* The owner replaced us (g_paste_history_reload_backend): this result comes
     * from the backend it just moved away from, so installing it would clobber
     * the freshly loaded one. */
    if (self->detached)
        return;

    /* A newer load superseded this one: leave load_in_progress set (the newer
     * load will clear it) and drop this stale result, so is_loading() does not
     * briefly report FALSE and let a save race the still-running load. */
    if (data->generation != self->load_generation)
        return;

    self->load_in_progress = FALSE;

    if (!load_result)
    {
        if (error)
            g_warning ("Failed to load history: %s", error->message);
        return;
    }

    self->loaded (self->owner, g_steal_pointer (&load_result->history), load_result->size,
                  data->save_after, load_result->readable);
}

/**
 * g_paste_history_saver_load:
 * @self: a #GPasteHistorySaver
 * @name: the history name to read
 * @save_after: whether the loaded history should be persisted back once installed
 *
 * Read @name in the background. When it completes (and has not been superseded
 * by a later load) the result is handed to the #GPasteHistorySaverLoadedFunc.
 */
G_PASTE_VISIBLE void
g_paste_history_saver_load (GPasteHistorySaver *self,
                            const gchar        *name,
                            gboolean            save_after)
{
    g_return_if_fail (_G_PASTE_IS_HISTORY_SAVER (self));

    self->load_in_progress = TRUE;
    self->load_generation++;

    GPasteHistorySaverLoadData *data = g_new (GPasteHistorySaverLoadData, 1);
    data->backend = g_object_ref (self->backend);
    data->name = g_strdup (name);
    data->generation = self->load_generation;
    /* save_after exists so a snapshot-rewriting backend persists its read-time
     * normalization (format upgrade, uuid dedup, truncation). An incremental
     * backend normalizes its storage on open instead, so writing the whole
     * history back after a load would be pure churn: drop the request. */
    data->save_after = save_after && !g_paste_storage_backend_is_incremental (self->backend);

    /* Hold our own ref for the task (see start_write): reload_backend may drop
     * the owner's ref to us while this load is still in flight. */
    g_autoptr (GTask) task = g_task_new (self->owner, NULL, g_paste_history_saver_load_done, g_object_ref (self));
    g_task_set_static_name (task, "gpaste-history-load");
    g_task_set_task_data (task, data, g_paste_history_saver_load_data_free);
    g_task_run_in_thread (task, g_paste_history_saver_load_task);
}

/**
 * g_paste_history_saver_abandon_load:
 * @self: a #GPasteHistorySaver
 *
 * Drop the result of the load currently in flight, if any. The read itself is
 * already running on a worker thread and cannot be interrupted, but its result
 * is discarded instead of being installed, and is_loading() reports %FALSE right
 * away. Meant for a caller that just invalidated what is being read (see
 * g_paste_history_delete()), so the load cannot resurrect it afterwards.
 */
G_PASTE_VISIBLE void
g_paste_history_saver_abandon_load (GPasteHistorySaver *self)
{
    g_return_if_fail (_G_PASTE_IS_HISTORY_SAVER (self));

    /* The same mechanism a superseding load relies on: bumping the generation
     * makes load_done() drop the stale result. Unlike that case there is no
     * newer load to clear @load_in_progress, so clear it here. */
    self->load_generation++;
    self->load_in_progress = FALSE;
}

/**
 * g_paste_history_saver_is_loading:
 * @self: a #GPasteHistorySaver
 *
 * Returns: whether a load is currently in progress
 */
G_PASTE_VISIBLE gboolean
g_paste_history_saver_is_loading (const GPasteHistorySaver *self)
{
    g_return_val_if_fail (_G_PASTE_IS_HISTORY_SAVER (self), FALSE);

    return self->load_in_progress;
}

/****************/
/* GObject glue */
/****************/

static void
g_paste_history_saver_dispose (GObject *object)
{
    GPasteHistorySaver *self = G_PASTE_HISTORY_SAVER (object);

    g_queue_clear_full (&self->pending, g_paste_history_saver_write_free);
    g_clear_object (&self->backend);

    G_OBJECT_CLASS (g_paste_history_saver_parent_class)->dispose (object);
}

static void
g_paste_history_saver_finalize (GObject *object)
{
    GPasteHistorySaver *self = G_PASTE_HISTORY_SAVER (object);

    g_mutex_clear (&self->drain_mutex);
    g_cond_clear (&self->drain_cond);

    G_OBJECT_CLASS (g_paste_history_saver_parent_class)->finalize (object);
}

static void
g_paste_history_saver_class_init (GPasteHistorySaverClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_history_saver_dispose;
    G_OBJECT_CLASS (klass)->finalize = g_paste_history_saver_finalize;
}

static void
g_paste_history_saver_init (GPasteHistorySaver *self)
{
    g_queue_init (&self->pending);
    g_mutex_init (&self->drain_mutex);
    g_cond_init (&self->drain_cond);
}

/**
 * g_paste_history_saver_new:
 * @backend: the #GPasteStorageBackend to read and write through
 * @owner: the GObject driving this saver; used as the async source so it (and
 *         therefore this saver, which it owns) stays alive during operations,
 *         and passed back to @loaded
 * @loaded: (scope notified): invoked when an async load completes
 *
 * Create a new instance of #GPasteHistorySaver
 *
 * Returns: a newly allocated #GPasteHistorySaver
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteHistorySaver *
g_paste_history_saver_new (GPasteStorageBackend        *backend,
                           gpointer                     owner,
                           GPasteHistorySaverLoadedFunc loaded)
{
    g_return_val_if_fail (_G_PASTE_IS_STORAGE_BACKEND (backend), NULL);
    g_return_val_if_fail (G_IS_OBJECT (owner), NULL);
    g_return_val_if_fail (loaded, NULL);

    GPasteHistorySaver *self = g_object_new (G_PASTE_TYPE_HISTORY_SAVER, NULL);

    self->backend = g_object_ref (backend);
    self->owner = owner;
    self->loaded = loaded;

    return self;
}
