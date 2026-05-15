/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <adwaita.h>

#include <gpaste-ui-edit-item.h>

struct _GPasteUiEditItem
{
    GPasteUiItemAction parent_instance;
};

typedef struct
{
    GtkWindow *rootwin;
} GPasteUiEditItemPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UiEditItem, ui_edit_item, G_PASTE_TYPE_UI_ITEM_ACTION)

typedef struct
{
    GtkWindow *rootwin;
    gchar     *uuid;
} CallbackData;

typedef struct
{
    GPasteClient  *client;
    gchar         *uuid;
    GtkTextBuffer *buffer;
} EditItemDialogData;

static void
on_edit_response (GObject      *dialog,
                  GAsyncResult *result,
                  gpointer      user_data)
{
    g_autofree EditItemDialogData *data = user_data;
    g_autoptr (GPasteClient) client = data->client;
    g_autofree gchar *uuid = data->uuid;
    g_autoptr (GtkTextBuffer) buffer = data->buffer;
    const gchar *response = adw_alert_dialog_choose_finish (ADW_ALERT_DIALOG (dialog), result);

    if (g_strcmp0 (response, "confirm") == 0)
    {
        GtkTextIter start, end;

        gtk_text_buffer_get_bounds (buffer, &start, &end);
        g_autofree gchar *txt = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
        if (txt && *txt)
            g_paste_client_replace (client, uuid, txt, NULL, NULL);
    }
}

static void
on_item_ready (GObject      *source_object,
               GAsyncResult *res,
               gpointer      user_data)
{
    g_autofree CallbackData *data = user_data;
    g_autofree gchar *uuid = data->uuid;
    g_autoptr (GtkWindow) rootwin = data->rootwin;
    GPasteClient *client = G_PASTE_CLIENT (source_object);
    g_autofree gchar *old_item = g_paste_client_get_raw_element_finish (client, res, NULL);

    if (!old_item)
        return;

    AdwAlertDialog *dialog = ADW_ALERT_DIALOG (adw_alert_dialog_new (PACKAGE_STRING, NULL));
    GtkWidget *text = gtk_text_view_new ();
    GtkTextView *tv = GTK_TEXT_VIEW (text);
    GtkTextBuffer *buf = gtk_text_view_get_buffer (tv);
    GtkWidget *scroll = gtk_scrolled_window_new ();
    GtkScrolledWindow *sw = GTK_SCROLLED_WINDOW (scroll);

    gtk_text_view_set_wrap_mode (tv, GTK_WRAP_WORD);
    gtk_text_buffer_set_text (buf, old_item, -1);
    gtk_scrolled_window_set_min_content_height (sw, 300);
    gtk_scrolled_window_set_min_content_width (sw, 600);
    gtk_scrolled_window_set_child (sw, text);
    gtk_widget_set_vexpand (scroll, TRUE);

    adw_alert_dialog_add_responses (dialog,
                                    "cancel",  _("Cancel"),
                                    "confirm", _("Edit"),
                                    NULL);
    adw_alert_dialog_set_extra_child (dialog, scroll);

    EditItemDialogData *dialog_data = g_new (EditItemDialogData, 1);
    dialog_data->client = g_object_ref (client);
    dialog_data->uuid = g_strdup (uuid);
    dialog_data->buffer = g_object_ref (buf);

    adw_alert_dialog_choose (dialog, GTK_WIDGET (rootwin), NULL, on_edit_response, dialog_data);
}

static void
g_paste_ui_edit_item_activate (GPasteUiItemAction *self,
                               GPasteClient       *client,
                               const gchar        *uuid)
{
    CallbackData *data = g_new (CallbackData, 1);
    GPasteUiEditItemPrivate *priv = g_paste_ui_edit_item_get_instance_private (G_PASTE_UI_EDIT_ITEM (self));

    data->rootwin = g_object_ref (priv->rootwin);
    data->uuid = g_strdup (uuid);

    g_paste_client_get_raw_element (client, uuid, on_item_ready, data);
}

static void
g_paste_ui_edit_item_class_init (GPasteUiEditItemClass *klass)
{
    G_PASTE_UI_ITEM_ACTION_CLASS (klass)->activate = g_paste_ui_edit_item_activate;
}

static void
g_paste_ui_edit_item_init (GPasteUiEditItem *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_ui_edit_item_new:
 * @client: a #GPasteClient
 * @rootwin: the root #GtkWindow
 *
 * Create a new instance of #GPasteUiEditItem
 *
 * Returns: a newly allocated #GPasteUiEditItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_edit_item_new (GPasteClient *client,
                          GtkWindow    *rootwin)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = g_paste_ui_item_action_new (G_PASTE_TYPE_UI_EDIT_ITEM, client, "accessories-text-editor-symbolic", _("Edit"));
    GPasteUiEditItemPrivate *priv = g_paste_ui_edit_item_get_instance_private (G_PASTE_UI_EDIT_ITEM (self));

    priv->rootwin = rootwin;

    return self;
}
