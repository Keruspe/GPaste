// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-edit-item.h>
#include <gpaste-ui-window.h>

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
    GtkWindow     *rootwin;
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
    g_autoptr (GtkWindow) rootwin = data->rootwin;
    const gchar *response = adw_alert_dialog_choose_finish (ADW_ALERT_DIALOG (dialog), result);

    if (g_strcmp0 (response, "confirm") == 0)
    {
        GtkTextIter start, end;

        gtk_text_buffer_get_bounds (buffer, &start, &end);
        g_autofree gchar *txt = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
        if (txt && *txt)
        {
            g_paste_client_replace (client, uuid, txt,
                                    g_paste_ui_report_string_cb,
                                    g_paste_ui_report_string (GTK_WIDGET (rootwin), g_paste_client_replace_finish,
                                                              _("Could not save the edited item")));
        }
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
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClientItem) item = g_paste_client_get_item_finish (client, res, &error);

    /* Without it there is no dialog to show, and the Edit the user asked for
     * would simply not happen. */
    if (!item)
    {
        /* A reply that parsed into no item sets no error: the uuid or the value
         * did not survive validation, which a daemon of another version can
         * well produce. */
        g_warning ("Could not read the item to edit: %s", (error) ? error->message : "the daemon answered with no usable item");

        if (G_PASTE_IS_UI_WINDOW (rootwin))
            g_paste_ui_window_toast (G_PASTE_UI_WINDOW (rootwin), _("Could not read the item to edit"));

        return;
    }

    const gchar *old_item = g_paste_client_item_get_value (item);

    GtkTextBuffer *buf = NULL;
    AdwAlertDialog *dialog = g_paste_gtk_util_text_dialog (_("Edit"), old_item, &buf);

    EditItemDialogData *dialog_data = g_new (EditItemDialogData, 1);
    dialog_data->client = g_object_ref (client);
    dialog_data->uuid = g_strdup (uuid);
    dialog_data->buffer = g_object_ref (buf);
    dialog_data->rootwin = g_object_ref (rootwin);

    adw_alert_dialog_choose (dialog, GTK_WIDGET (rootwin), NULL, on_edit_response, dialog_data);
}

/**
 * g_paste_ui_edit_item_show:
 * @client: a #GPasteClient
 * @rootwin: the root #GtkWindow
 * @uuid: the uuid of the item to edit
 *
 * Read an item back and offer its text for editing
 */
void
g_paste_ui_edit_item_show (GPasteClient *client,
                           GtkWindow    *rootwin,
                           const gchar  *uuid)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (client));
    g_return_if_fail (GTK_IS_WINDOW (rootwin));
    g_return_if_fail (uuid);

    CallbackData *data = g_new (CallbackData, 1);

    data->rootwin = g_object_ref (rootwin);
    data->uuid = g_strdup (uuid);

    /* The plain getter: Edit is only offered for a text item, whose display
     * string is its value. */
    g_paste_client_get_item (client, uuid, on_item_ready, data);
}
