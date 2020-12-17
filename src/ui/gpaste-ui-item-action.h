/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_ITEM_ACTION_H__
#define __G_PASTE_UI_ITEM_ACTION_H__

#include <gpaste-client.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_ITEM_ACTION (g_paste_ui_item_action_get_type ())

G_PASTE_DERIVABLE_TYPE (UiItemAction, ui_item_action, UI_ITEM_ACTION, GtkButton)

struct _GPasteUiItemActionClass
{
    GtkButtonClass parent_class;

    /*< pure virtual >*/
    void (*activate) (GPasteUiItemAction *self,
                      GPasteClient       *client,
                      const gchar        *uuid);
};

void g_paste_ui_item_action_set_uuid (GPasteUiItemAction *self,
                                      const gchar        *uuid);

GtkWidget *g_paste_ui_item_action_new (GType         type,
                                       GPasteClient *client,
                                       const gchar  *icon_name,
                                       const gchar  *tooltip);

G_END_DECLS

#endif /*__G_PASTE_UI_ITEM_ACTION_H__*/
