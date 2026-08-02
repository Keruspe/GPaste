// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-dialog.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>

typedef struct {
    GPasteGtkConfirmDialogCallback callback;
    gpointer                       user_data;
} GPasteGtkConfirmDialogCallbackData;

static void
on_confirm_response (GObject      *dialog,
                     GAsyncResult *result,
                     gpointer      user_data)
{
    g_autofree GPasteGtkConfirmDialogCallbackData *data = user_data;
    const gchar *response = adw_alert_dialog_choose_finish (ADW_ALERT_DIALOG (dialog), result);

    data->callback (g_strcmp0 (response, "confirm") == 0, data->user_data);
}

/**
 * g_paste_gtk_util_confirm_dialog:
 * @parent: (nullable): the parent #GtkWindow
 * @action: the label for the confirm button
 * @msg: the message to display
 * @on_confirmation: (closure user_data) (scope notified): handler to invoke when we get a confirmation
 *
 * Ask the user for confirmation
 */
G_PASTE_VISIBLE void
g_paste_gtk_util_confirm_dialog (GtkWindow                     *parent,
                                 const gchar                   *action,
                                 const gchar                   *msg,
                                 GPasteGtkConfirmDialogCallback on_confirmation,
                                 gpointer                       user_data)
{
    g_return_if_fail (!parent || GTK_IS_WINDOW (parent));
    g_return_if_fail (action);
    g_return_if_fail (g_utf8_validate (msg, -1, NULL));
    g_return_if_fail (on_confirmation);

    GPasteGtkConfirmDialogCallbackData *data = g_new (GPasteGtkConfirmDialogCallbackData, 1);
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG (adw_alert_dialog_new (PACKAGE_STRING, msg));

    data->callback = on_confirmation;
    data->user_data = user_data;

    adw_alert_dialog_add_responses (dialog,
                                    "cancel",  _("Cancel"),
                                    "confirm", action,
                                    NULL);
    adw_alert_dialog_set_response_appearance (dialog, "confirm", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_choose (dialog, GTK_WIDGET (parent), NULL, on_confirm_response, data);
}

/**
 * g_paste_gtk_util_get_image_finish:
 * @client: the #GPasteClient the g_paste_client_get_image() call was made on
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to g_paste_client_get_image()
 * @error: a #GError
 *
 * Finish a g_paste_client_get_image() call as a ready-to-display texture. The
 * core client stays toolkit-free and hands image items over as bytes; this is
 * the GTK-side conversion for its consumers.
 *
 * Returns: (transfer full) (nullable): the image as a newly allocated #GdkTexture
 */
G_PASTE_VISIBLE GdkTexture *
g_paste_gtk_util_get_image_finish (GPasteClient *client,
                                   GAsyncResult *result,
                                   GError      **error)
{
    g_autoptr (GBytes) bytes = g_paste_client_get_image_finish (client, result, error);

    if (!bytes)
        return NULL;

    return gdk_texture_new_from_bytes (bytes, error);
}

typedef struct {
    GPasteClient *client;
    gchar        *history;
} EmptyHistoryCallbackData;

static void
empty_history_callback (gboolean confirmed,
                        gpointer user_data)
{
    g_autofree EmptyHistoryCallbackData *data = user_data;
    g_autoptr (GPasteClient) client = data->client;
    g_autofree gchar *history = data->history;

    if (confirmed)
        g_paste_client_empty_history (client, history, NULL, NULL);
}

/**
 * g_paste_gtk_util_empty_history:
 * @parent_window: (nullable): the parent #GtkWindow
 * @client: a #GPasteClient instance
 * @settings: a #GPasteSettings instance
 * @history: the name of the history to empty
 *
 * Empty history after prompting user for confirmation
 */
G_PASTE_VISIBLE void
g_paste_gtk_util_empty_history (GtkWindow      *parent_window,
                                GPasteClient   *client,
                                GPasteSettings *settings,
                                const gchar    *history)
{
    g_return_if_fail (!parent_window || GTK_IS_WINDOW (parent_window));
    g_return_if_fail (G_PASTE_IS_CLIENT (client));
    g_return_if_fail (G_PASTE_IS_SETTINGS (settings));
    g_return_if_fail (history);

    if (g_paste_settings_get_empty_history_confirmation (settings))
    {
        EmptyHistoryCallbackData *data = g_new (EmptyHistoryCallbackData, 1);

        data->client = g_object_ref (client);
        data->history = g_strdup (history);

        /* Translators: %s is the name of the history being emptied. */
        g_autofree gchar *msg = g_strdup_printf (_("Do you really want to empty \"%s\"?"), history);
        g_paste_gtk_util_confirm_dialog (parent_window, _("Empty"), msg, empty_history_callback, data);
    }
    else
        g_paste_client_empty_history (client, history, NULL, NULL);
}

/**
 * g_paste_gtk_util_show_window:
 * @application: a #GtkApplication
 *
 * Present the application's window to user
 */
G_PASTE_VISIBLE void
g_paste_gtk_util_show_window (GApplication *application)
{
    g_return_if_fail (GTK_IS_APPLICATION (application));

    for (GList *wins = gtk_application_get_windows (GTK_APPLICATION (application)); wins; wins = g_list_next (wins))
    {
        if (GTK_IS_WIDGET (wins->data) && gtk_widget_get_realized (wins->data))
            gtk_window_present (wins->data);
    }
}
