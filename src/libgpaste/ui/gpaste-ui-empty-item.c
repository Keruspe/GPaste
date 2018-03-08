/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-empty-item.h>

struct _GPasteUiEmptyItem
{
    GPasteUiItemSkeleton parent_instance;
};

G_PASTE_DEFINE_TYPE (UiEmptyItem, ui_empty_item, G_PASTE_TYPE_UI_ITEM_SKELETON)

static void
g_paste_ui_empty_item_show_text (GPasteUiEmptyItem *self,
                                 const gchar       *text)
{
    g_paste_ui_item_skeleton_set_text (G_PASTE_UI_ITEM_SKELETON (self), text);
    gtk_widget_show (GTK_WIDGET (self));
}

/**
 * g_paste_ui_empty_show_no_result:
 * @self: a #GPasteUiEmptyItem instance
 *
 * Show a no result message
 */
G_PASTE_VISIBLE void
g_paste_ui_empty_item_show_no_result (GPasteUiEmptyItem *self)
{
    g_return_if_fail (G_PASTE_IS_UI_EMPTY_ITEM (self));

    g_paste_ui_empty_item_show_text (self, _("(No result)"));
}

/**
 * g_paste_ui_empty_show_empty:
 * @self: a #GPasteUiEmptyItem instance
 *
 * Show an empty message
 */
G_PASTE_VISIBLE void
g_paste_ui_empty_item_show_empty (GPasteUiEmptyItem *self)
{
    g_return_if_fail (G_PASTE_IS_UI_EMPTY_ITEM (self));

    g_paste_ui_empty_item_show_text (self, _("(Empty)"));
}

static void
g_paste_ui_empty_item_class_init (GPasteUiEmptyItemClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_ui_empty_item_init (GPasteUiEmptyItem *self)
{
    g_paste_ui_item_skeleton_set_text (G_PASTE_UI_ITEM_SKELETON (self), _("(Couldn't connect to GPaste daemon)"));
}

/**
 * g_paste_ui_empty_item_new:
 * @client: a #GPasteClient instance
 * @settings: a #GPasteSettings instance
 * @rootwin: the root #GtkWindow
 *
 * Create a new instance of #GPasteUiEmptyItem
 *
 * Returns: a newly allocated #GPasteUiEmptyItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_empty_item_new (GPasteClient   *client,
                           GPasteSettings *settings,
                           GtkWindow      *rootwin)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = g_paste_ui_item_skeleton_new (G_PASTE_TYPE_UI_EMPTY_ITEM, client, settings, rootwin);

    g_paste_ui_item_skeleton_set_activatable (G_PASTE_UI_ITEM_SKELETON (self), FALSE);

    return self;
}
