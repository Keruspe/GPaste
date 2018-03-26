/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_HISTORY_ACTIONS_H__
#define __G_PASTE_UI_HISTORY_ACTIONS_H__

#include <gpaste-settings.h>
#include <gpaste-ui-panel-history.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_HISTORY_ACTIONS (g_paste_ui_history_actions_get_type ())

G_PASTE_FINAL_TYPE (UiHistoryActions, ui_history_actions, UI_HISTORY_ACTIONS, GtkPopover)

void g_paste_ui_history_actions_set_relative_to (GPasteUiHistoryActions *self,
                                                 GPasteUiPanelHistory   *history);

GtkWidget *g_paste_ui_history_actions_new (GPasteClient   *client,
                                           GPasteSettings *settings,
                                           GtkWindow      *rootwin);

G_END_DECLS

#endif /*__G_PASTE_UI_HISTORY_ACTIONS_H__*/
