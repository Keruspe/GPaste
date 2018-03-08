/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_EMPTY_ITEM_H__
#define __G_PASTE_UI_EMPTY_ITEM_H__

#include <gpaste-ui-item-skeleton.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_EMPTY_ITEM (g_paste_ui_empty_item_get_type ())

G_PASTE_FINAL_TYPE (UiEmptyItem, ui_empty_item, UI_EMPTY_ITEM, GPasteUiItemSkeleton)

void g_paste_ui_empty_item_show_no_result (GPasteUiEmptyItem *self);
void g_paste_ui_empty_item_show_empty     (GPasteUiEmptyItem *self);

GtkWidget *g_paste_ui_empty_item_new (GPasteClient   *client,
                                      GPasteSettings *settings,
                                      GtkWindow      *rootwin);

G_END_DECLS

#endif /*__G_PASTE_UI_EMPTY_ITEM_H__*/
