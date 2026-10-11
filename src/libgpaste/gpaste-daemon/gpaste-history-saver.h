// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-storage-backend.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_HISTORY_SAVER (g_paste_history_saver_get_type ())

G_PASTE_FINAL_TYPE (HistorySaver, history_saver, HISTORY_SAVER, GObject)

/**
 * GPasteHistorySaverLoadedFunc:
 * @user_data: the @owner passed to g_paste_history_saver_new()
 * @history: (transfer full) (element-type GPasteItem): the loaded items
 * @size: the total size of the loaded items
 * @save_after: whether the load was triggered by a switch and should be persisted back
 * @readable: %FALSE when the history exists on disk but could not be read back,
 *            so @history is empty only because the read failed and must never be
 *            written back over the intact data
 *
 * Called on the main thread when an asynchronous load finishes and is still
 * current (i.e. not superseded by a newer load).
 */
typedef void (*GPasteHistorySaverLoadedFunc) (gpointer  user_data,
                                              GList    *history,
                                              gsize     size,
                                              gboolean  save_after,
                                              gboolean  readable);

/**
 * GPasteHistorySaverDivergedFunc:
 * @user_data: the @owner passed to g_paste_history_saver_new()
 * @name: the history whose store a write left behind
 *
 * Called on the main thread when a write other than a full one has left the
 * store of @name behind the history (see g_paste_storage_backend_has_diverged()),
 * for the owner to record a full write of it.
 */
typedef void (*GPasteHistorySaverDivergedFunc) (gpointer     user_data,
                                                const gchar *name);

/* The kind of change being persisted. Incremental backends (e.g. SQLite) act on
 * the supplied item/uuid; the file backend ignores them and rewrites the whole
 * snapshot, so for it every kind behaves like %G_PASTE_HISTORY_SAVE_FULL. */
typedef enum
{
    G_PASTE_HISTORY_SAVE_FULL,    /* rewrite the whole history */
    G_PASTE_HISTORY_SAVE_ADD,     /* @item was prepended */
    G_PASTE_HISTORY_SAVE_REMOVE,  /* @uuid was removed */
    G_PASTE_HISTORY_SAVE_REPLACE, /* @uuid was replaced by @item */
    G_PASTE_HISTORY_SAVE_CLEAR,   /* the history was emptied */
} GPasteHistorySaveOp;

/********************/
/* Async operations */
/********************/

void     g_paste_history_saver_record        (GPasteHistorySaver  *self,
                                              GPasteHistorySaveOp  op,
                                              const gchar         *name,
                                              GPasteItem          *item,
                                              const gchar         *uuid,
                                              GList               *history,
                                              guint64              count);
void     g_paste_history_saver_load          (GPasteHistorySaver *self,
                                              const gchar        *name,
                                              gboolean            save_after);
void     g_paste_history_saver_load_when_released (GPasteHistorySaver *self,
                                                   const gchar        *name,
                                                   gboolean            save_after);
void     g_paste_history_saver_drain         (GPasteHistorySaver *self);
void     g_paste_history_saver_drain_history (GPasteHistorySaver *self,
                                              const gchar        *name);
gboolean g_paste_history_saver_count_pending (GPasteHistorySaver *self,
                                              const gchar        *name,
                                              guint64            *count);
void     g_paste_history_saver_detach        (GPasteHistorySaver *self);
void     g_paste_history_saver_abandon_load  (GPasteHistorySaver *self);
void     g_paste_history_saver_hold_loads    (GPasteHistorySaver *self,
                                              gboolean            held);
gboolean g_paste_history_saver_is_loading    (GPasteHistorySaver *self);

/****************/
/* Constructor  */
/****************/

GPasteHistorySaver *g_paste_history_saver_new (GPasteStorageBackend          *backend,
                                               gpointer                       owner,
                                               GPasteHistorySaverLoadedFunc   loaded,
                                               GPasteHistorySaverDivergedFunc diverged);

G_END_DECLS
