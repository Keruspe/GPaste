/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_HISTORY_H__
#define __G_PASTE_UI_HISTORY_H__

#include <gpaste-ui-panel.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_HISTORY (g_paste_ui_history_get_type ())

G_PASTE_FINAL_TYPE (UiHistory, ui_history, UI_HISTORY, GtkListBox)

void g_paste_ui_history_search (GPasteUiHistory *self,
                                const gchar     *search);

gboolean g_paste_ui_history_select_first (GPasteUiHistory *self);

GtkWidget *g_paste_ui_history_new (GPasteClient   *client,
                                   GPasteSettings *settings,
                                   GPasteUiPanel  *panel,
                                   GtkWindow      *rootwin);

G_END_DECLS

#endif /*__G_PASTE_UI_HISTORY_H__*/
