// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-macros.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_HISTORY_ITEM (g_paste_ui_history_item_get_type ())

G_PASTE_FINAL_TYPE (UiHistoryItem, ui_history_item, UI_HISTORY_ITEM, GObject)

const gchar *g_paste_ui_history_item_get_uuid   (GPasteUiHistoryItem *self);
GtkWidget   *g_paste_ui_history_item_get_widget (GPasteUiHistoryItem *self);
void         g_paste_ui_history_item_set_widget (GPasteUiHistoryItem *self,
                                                 GtkWidget           *widget);

#define G_PASTE_TYPE_UI_HISTORY_MODEL (g_paste_ui_history_model_get_type ())

G_PASTE_FINAL_TYPE (UiHistoryModel, ui_history_model, UI_HISTORY_MODEL, GObject)

GPasteUiHistoryItem *g_paste_ui_history_model_peek (GPasteUiHistoryModel *self,
                                                    guint64               position);

gboolean g_paste_ui_history_model_set_size (GPasteUiHistoryModel *self,
                                            guint64               size);
void g_paste_ui_history_model_set_uuids    (GPasteUiHistoryModel *self,
                                            const gchar * const  *uuids);
void g_paste_ui_history_model_invalidate   (GPasteUiHistoryModel *self,
                                            guint64               position,
                                            guint64               n_items);
void g_paste_ui_history_model_item_replaced (GPasteUiHistoryModel *self,
                                             guint64               position);
void g_paste_ui_history_model_item_replaced_by_uuid (GPasteUiHistoryModel *self,
                                                     const gchar          *uuid);

GPasteUiHistoryModel *g_paste_ui_history_model_new (void);

G_END_DECLS
