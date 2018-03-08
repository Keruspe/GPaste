/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_PANEL_HISTORY_H__
#define __G_PASTE_UI_PANEL_HISTORY_H__

#include <gpaste-client.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_PANEL_HISTORY (g_paste_ui_panel_history_get_type ())

G_PASTE_FINAL_TYPE (UiPanelHistory, ui_panel_history, UI_PANEL_HISTORY, GtkListBoxRow)

void g_paste_ui_panel_history_activate  (GPasteUiPanelHistory *self);
void g_paste_ui_panel_history_set_length (GPasteUiPanelHistory *self,
                                          guint64               length);

const gchar *g_paste_ui_panel_history_get_history (const GPasteUiPanelHistory *self);

GtkWidget *g_paste_ui_panel_history_new (GPasteClient *client,
                                         const gchar  *history);

G_END_DECLS

#endif /*__G_PASTE_UI_PANEL_HISTORY_H__*/
