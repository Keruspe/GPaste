/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gpaste-ui-empty-item.h>

struct _GPasteUiEmptyItem
{
    GPasteUiItemSkeleton parent_instance;
};

G_PASTE_DEFINE_TYPE (UiEmptyItem, ui_empty_item, G_PASTE_TYPE_UI_ITEM_SKELETON)

static void
g_paste_ui_empty_item_class_init (GPasteUiEmptyItemClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_ui_empty_item_init (GPasteUiEmptyItem *self)
{
    g_paste_ui_item_skeleton_set_text (G_PASTE_UI_ITEM_SKELETON (self), _("(Empty)"));
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
