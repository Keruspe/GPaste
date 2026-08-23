// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-new-item.h>

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
            g_paste_client_add_text (client, txt, NULL, NULL);
    }
}

/**
 * g_paste_ui_new_item_show:
 * @client: a #GPasteClient
 * @rootwin: the root #GtkWindow
 *
 * Ask the user for the text of a new item, and add it
 */
void
g_paste_ui_new_item_show (GPasteClient *client,
                          GtkWindow    *rootwin)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (client));
    g_return_if_fail (GTK_IS_WINDOW (rootwin));

    GtkTextBuffer *buf = NULL;
    AdwAlertDialog *dialog = g_paste_gtk_util_text_dialog (_("Add New Item"), NULL, &buf);

    NewItemDialogData *data = g_new (NewItemDialogData, 1);
    data->client = g_object_ref (client);
    data->buffer = g_object_ref (buf);

    adw_alert_dialog_choose (dialog, GTK_WIDGET (rootwin), NULL, on_new_item_response, data);
}
