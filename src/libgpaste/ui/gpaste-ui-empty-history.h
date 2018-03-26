/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_EMPTY_HISTORY_H__
#define __G_PASTE_UI_EMPTY_HISTORY_H__

#include <gpaste-ui-history-action.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_EMPTY_HISTORY (g_paste_ui_empty_history_get_type ())

G_PASTE_FINAL_TYPE (UiEmptyHistory, ui_empty_history, UI_EMPTY_HISTORY, GPasteUiHistoryAction)

GtkWidget *g_paste_ui_empty_history_new (GPasteClient   *client,
                                         GPasteSettings *settings,
                                         GtkWidget      *actions,
                                         GtkWindow      *rootwin);

G_END_DECLS

#endif /*__G_PASTE_UI_EMPTY_HISTORY_H__*/
