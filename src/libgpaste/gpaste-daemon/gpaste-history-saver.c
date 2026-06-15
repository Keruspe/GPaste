// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-history-saver.h>

#include <gio/gio.h>

/* Owns the asynchronous persistence of a history: it writes snapshots handed to
 * it (coalescing concurrent requests) and loads histories in the background,
 * handing the result back through a callback. It never touches the live history
 * list, so it shares no locking with the model.
 *
 * All of its state is only ever touched on the main thread: g_paste_history_saver_save
 * / _load run there, and the GTask completion callbacks fire on the thread-default
 * context they were created on. The @owner GObject is used as the GTask source so
 * it stays alive (and, since it owns this saver, keeps the saver alive too) for the
 * whole duration of every in-flight operation. */
struct _GPasteHistorySaver
{
    GObject parent_instance;
};

typedef struct
{
    GPasteStorageBackend        *backend;
    gpointer                     owner; /* not ref'd: owns us, outlives us */
    GPasteHistorySaverLoadedFunc loaded;

    gboolean                     write_in_progress;
    /* The most recent snapshot waiting to be written, if any. */
    gboolean                     write_pending;
    gchar                       *pending_name;
    GList                       *pending_history;

    gboolean                     load_in_progress;
    guint64                      load_generation;
} GPasteHistorySaverPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (HistorySaver, history_saver, G_TYPE_OBJECT)

/****************/
/* Write path   */
/****************/

typedef struct
{
    GPasteStorageBackend *backend;
    gchar                *name;
    GList                *history;
} GPasteHistorySaverWriteData;

static void
g_paste_history_saver_write_data_free (gpointer data)
{
    g_autofree GPasteHistorySaverWriteData *d = data;
    g_clear_object (&d->backend);
    g_clear_pointer (&d->name, g_free);
    g_clear_list (&d->history, g_object_unref);
}

static void
g_paste_history_saver_write_task (GTask        *task,
                                  gpointer      source_object G_GNUC_UNUSED,
                                  gpointer      task_data,
                                  GCancellable *cancellable G_GNUC_UNUSED)
{
    const GPasteHistorySaverWriteData *data = task_data;
    g_paste_storage_backend_write_history (data->backend, data->name, data->history);
    g_task_return_boolean (task, TRUE);
}

static void g_paste_history_saver_write_done (GObject *source_object, GAsyncResult *result, gpointer user_data);

static void
g_paste_history_saver_start_write (GPasteHistorySaver *self)
{
    GPasteHistorySaverPrivate *priv = g_paste_history_saver_get_instance_private (self);

    if (priv->write_in_progress || !priv->write_pending)
        return;

    priv->write_in_progress = TRUE;
    priv->write_pending = FALSE;

    GPasteHistorySaverWriteData *data = g_new (GPasteHistorySaverWriteData, 1);
    data->backend = g_object_ref (priv->backend);
    data->name = g_steal_pointer (&priv->pending_name);
    data->history = g_steal_pointer (&priv->pending_history);

    g_autoptr (GTask) task = g_task_new (priv->owner, NULL, g_paste_history_saver_write_done, self);
    g_task_set_static_name (task, "gpaste-history-write");
    g_task_set_task_data (task, data, g_paste_history_saver_write_data_free);
    g_task_run_in_thread (task, g_paste_history_saver_write_task);
}

static void
g_paste_history_saver_write_done (GObject      *source_object G_GNUC_UNUSED,
                                  GAsyncResult *result        G_GNUC_UNUSED,
                                  gpointer      user_data)
{
    GPasteHistorySaver *self = user_data;
    GPasteHistorySaverPrivate *priv = g_paste_history_saver_get_instance_private (self);

    priv->write_in_progress = FALSE;

    /* A snapshot may have arrived while we were writing: flush the latest one. */
    g_paste_history_saver_start_write (self);
}

/**
 * g_paste_history_saver_save:
 * @self: a #GPasteHistorySaver
 * @name: the history name to write to
 * @history: (transfer full) (element-type GPasteItem): a snapshot of the items
 *
 * Persist @history under @name. If a write is already in flight, @history
 * replaces any previously queued snapshot and is written once it completes, so
 * only the most recent state ever reaches the disk.
 */
G_PASTE_VISIBLE void
g_paste_history_saver_save (GPasteHistorySaver *self,
                            const gchar        *name,
                            GList              *history)
{
    g_return_if_fail (_G_PASTE_IS_HISTORY_SAVER (self));

    GPasteHistorySaverPrivate *priv = g_paste_history_saver_get_instance_private (self);

    g_clear_list (&priv->pending_history, g_object_unref);
    g_clear_pointer (&priv->pending_name, g_free);

    priv->pending_name = g_strdup (name);
    priv->pending_history = history;
    priv->write_pending = TRUE;

    g_paste_history_saver_start_write (self);
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
    GList *history;
    gsize  size;
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

    g_paste_storage_backend_read_history (data->backend, data->name, &result->history, &result->size);
    g_task_return_pointer (task, result, (GDestroyNotify) g_paste_history_saver_load_result_free);
}

static void
g_paste_history_saver_load_done (GObject      *source_object G_GNUC_UNUSED,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
    GPasteHistorySaver *self = user_data;
    GPasteHistorySaverPrivate *priv = g_paste_history_saver_get_instance_private (self);
    const GPasteHistorySaverLoadData *data = g_task_get_task_data (G_TASK (result));

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteHistorySaverLoadResult) load_result = g_task_propagate_pointer (G_TASK (result), &error);

    priv->load_in_progress = FALSE;

    if (!load_result)
    {
        if (error)
            g_warning ("Failed to load history: %s", error->message);
        return;
    }

    if (data->generation == priv->load_generation)
        priv->loaded (priv->owner, g_steal_pointer (&load_result->history), load_result->size, data->save_after);
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

    GPasteHistorySaverPrivate *priv = g_paste_history_saver_get_instance_private (self);

    priv->load_in_progress = TRUE;
    priv->load_generation++;

    GPasteHistorySaverLoadData *data = g_new (GPasteHistorySaverLoadData, 1);
    data->backend = g_object_ref (priv->backend);
    data->name = g_strdup (name);
    data->generation = priv->load_generation;
    data->save_after = save_after;

    g_autoptr (GTask) task = g_task_new (priv->owner, NULL, g_paste_history_saver_load_done, self);
    g_task_set_static_name (task, "gpaste-history-load");
    g_task_set_task_data (task, data, g_paste_history_saver_load_data_free);
    g_task_run_in_thread (task, g_paste_history_saver_load_task);
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

    const GPasteHistorySaverPrivate *priv = _g_paste_history_saver_get_instance_private (self);

    return priv->load_in_progress;
}

/****************/
/* GObject glue */
/****************/

static void
g_paste_history_saver_dispose (GObject *object)
{
    GPasteHistorySaver *self = G_PASTE_HISTORY_SAVER (object);
    GPasteHistorySaverPrivate *priv = g_paste_history_saver_get_instance_private (self);

    g_clear_object (&priv->backend);
    g_clear_list (&priv->pending_history, g_object_unref);

    G_OBJECT_CLASS (g_paste_history_saver_parent_class)->dispose (object);
}

static void
g_paste_history_saver_finalize (GObject *object)
{
    GPasteHistorySaverPrivate *priv = g_paste_history_saver_get_instance_private (G_PASTE_HISTORY_SAVER (object));

    g_free (priv->pending_name);

    G_OBJECT_CLASS (g_paste_history_saver_parent_class)->finalize (object);
}

static void
g_paste_history_saver_class_init (GPasteHistorySaverClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_history_saver_dispose;
    object_class->finalize = g_paste_history_saver_finalize;
}

static void
g_paste_history_saver_init (GPasteHistorySaver *self G_GNUC_UNUSED)
{
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
    GPasteHistorySaverPrivate *priv = g_paste_history_saver_get_instance_private (self);

    priv->backend = g_object_ref (backend);
    priv->owner = owner;
    priv->loaded = loaded;

    return self;
}
