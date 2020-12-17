/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-upload-item.h>

struct _GPasteUiUploadItem
{
    GPasteUiItemAction parent_instance;
};

G_PASTE_DEFINE_TYPE (UiUploadItem, ui_upload_item, G_PASTE_TYPE_UI_ITEM_ACTION)

static void
g_paste_ui_upload_item_activate (GPasteUiItemAction *self G_GNUC_UNUSED,
                                 GPasteClient       *client,
                                 const gchar        *uuid)
{
    g_paste_client_upload (client, uuid, NULL, NULL);
}

static void
g_paste_ui_upload_item_class_init (GPasteUiUploadItemClass *klass)
{
    G_PASTE_UI_ITEM_ACTION_CLASS (klass)->activate = g_paste_ui_upload_item_activate;
}

static void
g_paste_ui_upload_item_init (GPasteUiUploadItem *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_ui_upload_item_new:
 * @client: a #GPasteClient
 *
 * Create a new instance of #GPasteUiUploadItem
 *
 * Returns: a newly allocated #GPasteUiUploadItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_upload_item_new (GPasteClient *client)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);

    return g_paste_ui_item_action_new (G_PASTE_TYPE_UI_UPLOAD_ITEM, client, "document-send-symbolic", _("Upload"));
}
