/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_ITEM_SKELETON_H__
#define __G_PASTE_UI_ITEM_SKELETON_H__

#include <gpaste-client.h>
#include <gpaste-settings.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_ITEM_SKELETON (g_paste_ui_item_skeleton_get_type ())

G_PASTE_DERIVABLE_TYPE (UiItemSkeleton, ui_item_skeleton, UI_ITEM_SKELETON, GtkListBoxRow)

struct _GPasteUiItemSkeletonClass
{
    GtkListBoxRowClass parent_class;
};

void g_paste_ui_item_skeleton_set_activatable (GPasteUiItemSkeleton *self,
                                               gboolean              activatable);
void g_paste_ui_item_skeleton_set_editable    (GPasteUiItemSkeleton *self,
                                               gboolean              editable);
void g_paste_ui_item_skeleton_set_uploadable  (GPasteUiItemSkeleton *self,
                                               gboolean              uploadable);

void g_paste_ui_item_skeleton_set_text   (GPasteUiItemSkeleton *self,
                                          const gchar          *text);
void g_paste_ui_item_skeleton_set_markup (GPasteUiItemSkeleton *self,
                                          const gchar          *markup);

void g_paste_ui_item_skeleton_set_index_and_uuid (GPasteUiItemSkeleton *self,
                                                  guint64               index,
                                                  const gchar          *uuid);

GtkLabel *g_paste_ui_item_skeleton_get_label (GPasteUiItemSkeleton *self);

GtkWidget *g_paste_ui_item_skeleton_new (GType           type,
                                         GPasteClient   *client,
                                         GPasteSettings *settings,
                                         GtkWindow      *rootwin);

G_END_DECLS

#endif /*__G_PASTE_UI_ITEM_SKELETON_H__*/
