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

GtkWidget *g_paste_ui_history_new (GPasteClient   *client,
                                   GPasteSettings *settings,
                                   GPasteUiPanel  *panel,
                                   GtkWindow      *rootwin);

G_END_DECLS

