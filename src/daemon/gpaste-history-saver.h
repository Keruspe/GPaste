// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-storage-backend.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_HISTORY_SAVER (g_paste_history_saver_get_type ())

G_PASTE_FINAL_TYPE (HistorySaver, history_saver, HISTORY_SAVER, GObject)

/**
 * GPasteHistorySaverLoadedFunc:
 * @user_data: the @owner passed to g_paste_history_saver_new()
 * @history: (transfer full) (element-type GPasteItem): the loaded items
 * @size: the total size of the loaded items
 * @save_after: whether the load was triggered by a switch and should be persisted back
 *
 * Called on the main thread when an asynchronous load finishes and is still
 * current (i.e. not superseded by a newer load).
 */
typedef void (*GPasteHistorySaverLoadedFunc) (gpointer  user_data,
                                              GList    *history,
                                              gsize     size,
                                              gboolean  save_after);

/********************/
/* Async operations */
/********************/

void     g_paste_history_saver_save       (GPasteHistorySaver *self,
                                           const gchar        *name,
                                           GList              *history);
void     g_paste_history_saver_load       (GPasteHistorySaver *self,
                                           const gchar        *name,
                                           gboolean            save_after);
gboolean g_paste_history_saver_is_loading (const GPasteHistorySaver *self);

/****************/
/* Constructor  */
/****************/

GPasteHistorySaver *g_paste_history_saver_new (GPasteStorageBackend        *backend,
                                               gpointer                     owner,
                                               GPasteHistorySaverLoadedFunc loaded);

G_END_DECLS
