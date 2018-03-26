/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_HISTORY_ACTION_H__
#define __G_PASTE_UI_HISTORY_ACTION_H__

#include <gpaste-client.h>
#include <gpaste-settings.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_HISTORY_ACTION (g_paste_ui_history_action_get_type ())

G_PASTE_DERIVABLE_TYPE (UiHistoryAction, ui_history_action, UI_HISTORY_ACTION, GtkButton)

struct _GPasteUiHistoryActionClass
{
    GtkButtonClass parent_class;

    /*< pure virtual >*/
    gboolean     (*activate) (GPasteUiHistoryAction *self,
                              GPasteClient          *client,
                              GPasteSettings        *settings,
                              GtkWindow             *rootwin,
                              const gchar           *history);
};

void g_paste_ui_history_action_set_history (GPasteUiHistoryAction *self,
                                            const gchar           *history);

GtkWidget *g_paste_ui_history_action_new (GType           type,
                                          GPasteClient   *client,
                                          GPasteSettings *settings,
                                          GtkWidget      *actions,
                                          GtkWindow      *rootwin,
                                          const gchar    *label);

G_END_DECLS

#endif /*__G_PASTE_UI_HISTORY_ACTION_H__*/
