// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gpaste/gpaste-client.h>
#include <gpaste/gpaste-settings.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_PANEL (g_paste_ui_panel_get_type ())

G_PASTE_FINAL_TYPE (UiPanel, ui_panel, UI_PANEL, GtkBox)

void g_paste_ui_panel_update_history_length (GPasteUiPanel *self,
                                             const gchar   *history,
                                             guint64        length);

GtkWidget *g_paste_ui_panel_new (GPasteClient   *client,
                                 GPasteSettings *settings,
                                 GtkWindow      *rootwin,
                                 GtkSearchEntry *search_entry);

G_END_DECLS

