// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-new-item.h>

struct _GPasteUiNewItem
{
    GtkButton parent_instance;

    GPasteClient *client;

    GtkWindow    *rootwin;
};

G_PASTE_DEFINE_TYPE (UiNewItem, ui_new_item, GTK_TYPE_BUTTON)

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
    GPasteUiNewItem *self = G_PASTE_UI_NEW_ITEM (button);
    GtkTextBuffer *buf = NULL;
    AdwAlertDialog *dialog = g_paste_gtk_util_text_dialog (_("Add new item"), NULL, &buf);

    NewItemDialogData *data = g_new (NewItemDialogData, 1);
    data->client = g_object_ref (self->client);
    data->buffer = g_object_ref (buf);

    adw_alert_dialog_choose (dialog, GTK_WIDGET (self->rootwin), NULL, on_new_item_response, data);
}

static void
g_paste_ui_new_item_dispose (GObject *object)
{
    GPasteUiNewItem *self = G_PASTE_UI_NEW_ITEM (object);

    g_clear_object (&self->client);

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
GtkWidget *
g_paste_ui_new_item_new (GtkWindow    *rootwin,
                         GPasteClient *client)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GPasteUiNewItem *self = g_object_new (G_PASTE_TYPE_UI_NEW_ITEM, NULL);

    self->client = g_object_ref (client);
    self->rootwin = rootwin;

    return GTK_WIDGET (self);
}
