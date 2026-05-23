// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <adwaita.h>

#include <gpaste-ui-new-item.h>

struct _GPasteUiNewItem
{
    GtkButton parent_instance;
};

typedef struct
{
    GPasteClient *client;

    GtkWindow    *rootwin;
} GPasteUiNewItemPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UiNewItem, ui_new_item, GTK_TYPE_BUTTON)

typedef struct
{
    GPasteClient  *client;
    GtkTextBuffer *buffer;
} NewItemDialogData;

static void
on_new_item_response (GObject      *dialog   G_GNUC_UNUSED,
                      GAsyncResult *result,
                      gpointer      user_data)
{
    g_autofree NewItemDialogData *data = user_data;
    g_autoptr (GPasteClient) client = data->client;
    g_autoptr (GtkTextBuffer) buffer = data->buffer;
    const gchar *response = adw_alert_dialog_choose_finish (ADW_ALERT_DIALOG (dialog), result);

    if (g_strcmp0 (response, "confirm") == 0)
    {
        GtkTextIter start, end;

        gtk_text_buffer_get_bounds (buffer, &start, &end);
        g_autofree gchar *txt = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
        if (txt && *txt)
            g_paste_client_add (client, txt, NULL, NULL);
    }
}

static void
g_paste_ui_new_item_clicked (GtkButton *button)
{
    const GPasteUiNewItemPrivate *priv = _g_paste_ui_new_item_get_instance_private (G_PASTE_UI_NEW_ITEM (button));
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG (adw_alert_dialog_new (PACKAGE_STRING, NULL));
    GtkWidget *text = gtk_text_view_new ();
    GtkTextView *tv = GTK_TEXT_VIEW (text);
    GtkWidget *scroll = gtk_scrolled_window_new ();
    GtkScrolledWindow *sw = GTK_SCROLLED_WINDOW (scroll);

    gtk_text_view_set_wrap_mode (tv, GTK_WRAP_WORD);
    gtk_scrolled_window_set_min_content_height (sw, 300);
    gtk_scrolled_window_set_min_content_width (sw, 600);
    gtk_scrolled_window_set_child (sw, text);
    gtk_widget_set_vexpand (scroll, TRUE);

    adw_alert_dialog_add_responses (dialog,
                                    "cancel",  _("Cancel"),
                                    "confirm", _("Add new item"),
                                    NULL);
    adw_alert_dialog_set_extra_child (dialog, scroll);

    NewItemDialogData *data = g_new (NewItemDialogData, 1);
    data->client = g_object_ref (priv->client);
    data->buffer = g_object_ref (gtk_text_view_get_buffer (tv));

    adw_alert_dialog_choose (dialog, GTK_WIDGET (priv->rootwin), NULL, on_new_item_response, data);
}

static void
g_paste_ui_new_item_dispose (GObject *object)
{
    GPasteUiNewItemPrivate *priv = g_paste_ui_new_item_get_instance_private (G_PASTE_UI_NEW_ITEM (object));

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_new_item_parent_class)->dispose (object);
}

static void
g_paste_ui_new_item_class_init (GPasteUiNewItemClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_new_item_dispose;
    GTK_BUTTON_CLASS (klass)->clicked = g_paste_ui_new_item_clicked;
}

static void
g_paste_ui_new_item_init (GPasteUiNewItem *self)
{
    GtkWidget *widget = GTK_WIDGET (self);

    gtk_widget_set_tooltip_text (widget, _("New item"));
    gtk_widget_set_valign (widget, GTK_ALIGN_CENTER);
    gtk_button_set_child (GTK_BUTTON (self), gtk_image_new_from_icon_name ("document-new-symbolic"));
}

/**
 * g_paste_ui_new_item_new:
 * @rootwin: the root #GtkWindow
 * @client: a #GPasteClient
 *
 * Create a new instance of #GPasteUiNewItem
 *
 * Returns: a newly allocated #GPasteUiNewItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_new_item_new (GtkWindow    *rootwin,
                         GPasteClient *client)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = g_object_new (G_PASTE_TYPE_UI_NEW_ITEM, NULL);
    GPasteUiNewItemPrivate *priv = g_paste_ui_new_item_get_instance_private (G_PASTE_UI_NEW_ITEM (self));

    priv->client = g_object_ref (client);
    priv->rootwin = rootwin;

    return self;
}
