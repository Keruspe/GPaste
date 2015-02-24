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
    GtkListBoxRow parent_instance;
};

G_DEFINE_TYPE (GPasteUiEmptyItem, g_paste_ui_empty_item, GTK_TYPE_LIST_BOX_ROW)

static void
g_paste_ui_empty_item_class_init (GPasteUiEmptyItemClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_ui_empty_item_init (GPasteUiEmptyItem *self)
{
    gtk_container_add (GTK_CONTAINER (self), gtk_label_new (_("(Empty)")));
}

/**
 * g_paste_ui_empty_item_new:
 *
 * Create a new instance of #GPasteUiEmptyItem
 *
 * Returns: a newly allocated #GPasteUiEmptyItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_empty_item_new (void)
{
    return gtk_widget_new (G_PASTE_TYPE_UI_EMPTY_ITEM,
                           "activatable", FALSE,
                           "selectable",  FALSE,
                           NULL);
}
