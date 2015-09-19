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

G_DEFINE_TYPE (GPasteUiEmptyItem, g_paste_ui_empty_item, G_PASTE_TYPE_UI_ITEM_SKELETON)

static void
g_paste_ui_empty_item_class_init (GPasteUiEmptyItemClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_ui_empty_item_init (GPasteUiEmptyItem *self)
{
    g_paste_ui_item_skeleton_set_text (G_PASTE_UI_ITEM_SKELETON (self), _("(Empty)"));
    gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (self), FALSE);
}

/**
 * g_paste_ui_empty_item_new:
 * @settings: a #GPasteSettings instance
 *
 * Create a new instance of #GPasteUiEmptyItem
 *
 * Returns: a newly allocated #GPasteUiEmptyItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_empty_item_new (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    return g_paste_ui_item_skeleton_new (G_PASTE_TYPE_UI_EMPTY_ITEM, settings);
}
