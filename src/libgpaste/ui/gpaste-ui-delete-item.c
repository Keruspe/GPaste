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

#include <gpaste-ui-delete-item.h>

struct _GPasteUiDeleteItem
{
    GPasteUiItemAction parent_instance;
};

G_DEFINE_TYPE (GPasteUiDeleteItem, g_paste_ui_delete_item, G_PASTE_TYPE_UI_ITEM_ACTION)

static void
g_paste_ui_delete_item_activate (GPasteUiItemAction *self G_GNUC_UNUSED,
                                 GPasteClient       *client,
                                 guint64             index)
{
    g_paste_client_delete (client, index, NULL, NULL);
}

static void
g_paste_ui_delete_item_class_init (GPasteUiDeleteItemClass *klass)
{
    G_PASTE_UI_ITEM_ACTION_CLASS (klass)->activate = g_paste_ui_delete_item_activate;
}

static void
g_paste_ui_delete_item_init (GPasteUiDeleteItem *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_ui_delete_item_new:
 * @client: a #GPasteClient
 *
 * Create a new instance of #GPasteUiDeleteItem
 *
 * Returns: a newly allocated #GPasteUiDeleteItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_delete_item_new (GPasteClient *client)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);

    return g_paste_ui_item_action_new (G_PASTE_TYPE_UI_DELETE_ITEM, client, "edit-delete-symbolic", _("Delete"));
}
