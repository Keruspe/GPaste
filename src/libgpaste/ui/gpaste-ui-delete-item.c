/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-delete-item.h>

struct _GPasteUiDeleteItem
{
    GPasteUiItemAction parent_instance;
};

G_PASTE_DEFINE_TYPE (UiDeleteItem, ui_delete_item, G_PASTE_TYPE_UI_ITEM_ACTION)

static void
g_paste_ui_delete_item_activate (GPasteUiItemAction *self G_GNUC_UNUSED,
                                 GPasteClient       *client,
                                 const gchar        *uuid)
{
    g_paste_client_delete (client, uuid, NULL, NULL);
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
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);

    return g_paste_ui_item_action_new (G_PASTE_TYPE_UI_DELETE_ITEM, client, "edit-delete-symbolic", _("Delete"));
}
