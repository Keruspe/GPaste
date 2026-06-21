// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gpaste-ui-panel.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_HISTORY (g_paste_ui_history_get_type ())

G_PASTE_FINAL_TYPE (UiHistory, ui_history, UI_HISTORY, GtkBox)

void g_paste_ui_history_search (GPasteUiHistory *self,
                                const gchar     *search);

gboolean g_paste_ui_history_select_first (GPasteUiHistory *self);
gboolean g_paste_ui_history_activate_index (GPasteUiHistory *self,
                                            guint64          index);

void g_paste_ui_history_set_selection_mode (GPasteUiHistory *self,
                                            gboolean         selection_mode);
GStrv g_paste_ui_history_get_selected_uuids (GPasteUiHistory *self,
                                             guint64         *length);

GtkWidget *g_paste_ui_history_new (GPasteClient   *client,
                                   GPasteSettings *settings,
                                   GPasteUiPanel  *panel,
                                   GtkWindow      *rootwin);

G_END_DECLS

