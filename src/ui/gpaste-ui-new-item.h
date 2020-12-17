/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_NEW_ITEM_H__
#define __G_PASTE_UI_NEW_ITEM_H__

#include <gpaste-client.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_NEW_ITEM (g_paste_ui_new_item_get_type ())

G_PASTE_FINAL_TYPE (UiNewItem, ui_new_item, UI_NEW_ITEM, GtkButton)

GtkWidget *g_paste_ui_new_item_new (GtkWindow    *rootwin,
                                    GPasteClient *client);

G_END_DECLS

#endif /*__G_PASTE_UI_NEW_ITEM_H__*/
