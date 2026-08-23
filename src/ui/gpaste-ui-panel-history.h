// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-client.h>

#include <adwaita.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_PANEL_HISTORY (g_paste_ui_panel_history_get_type ())

G_PASTE_FINAL_TYPE (UiPanelHistory, ui_panel_history, UI_PANEL_HISTORY, AdwSidebarItem)

void g_paste_ui_panel_history_activate   (GPasteUiPanelHistory *self);
void g_paste_ui_panel_history_set_length (GPasteUiPanelHistory *self,
                                          guint64               length);

const gchar *g_paste_ui_panel_history_get_history (GPasteUiPanelHistory *self);

GPasteUiPanelHistory *g_paste_ui_panel_history_new (GPasteClient *client,
                                                    const gchar  *history,
                                                    guint64       length);

G_END_DECLS

