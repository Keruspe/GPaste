// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-dialog.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>

typedef struct
{
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
 * @heading: the question being asked
 * @body: what confirming it does
 * @action: the label for the confirm button
 * @appearance: how the confirm button should look -- destructive only when
 *              something is actually lost
 * @on_confirmation: (closure user_data) (scope async): handler to invoke when we get a confirmation
 *
 * Ask the user for confirmation.
 *
 * The heading is the question, not the application's name and version: what an
 * alert is asking has to be readable from its title alone, and "GPaste 51.0"
 * asks nothing.
 */
G_PASTE_VISIBLE void
g_paste_gtk_util_confirm_dialog (GtkWindow                     *parent,
                                 const gchar                   *heading,
                                 const gchar                   *body,
                                 const gchar                   *action,
                                 AdwResponseAppearance          appearance,
                                 GPasteGtkConfirmDialogCallback on_confirmation,
                                 gpointer                       user_data)
{
    g_return_if_fail (!parent || GTK_IS_WINDOW (parent));
    g_return_if_fail (action);
    g_return_if_fail (g_utf8_validate (heading, -1, NULL));
    g_return_if_fail (!body || g_utf8_validate (body, -1, NULL));
    g_return_if_fail (on_confirmation);

    GPasteGtkConfirmDialogCallbackData *data = g_new (GPasteGtkConfirmDialogCallbackData, 1);
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG (adw_alert_dialog_new (heading, body));

    data->callback = on_confirmation;
    data->user_data = user_data;

    adw_alert_dialog_add_responses (dialog,
                                    "cancel",  _("Cancel"),
                                    "confirm", action,
                                    NULL);
    adw_alert_dialog_set_response_appearance (dialog, "confirm", appearance);
    /* Escape and Enter both land somewhere deliberate rather than on whatever
     * the default response happens to be. */
    adw_alert_dialog_set_default_response (dialog, "cancel");
    adw_alert_dialog_set_close_response (dialog, "cancel");
    adw_alert_dialog_choose (dialog, GTK_WIDGET (parent), NULL, on_confirm_response, data);
}

/**
 * g_paste_gtk_util_get_image_finish:
 * @client: the #GPasteClient the g_paste_client_get_image() call was made on
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Finish a g_paste_client_get_image() call as a ready-to-display texture. The
 * core client stays toolkit-free and hands image items over as bytes; this is
 * the GTK-side conversion for its consumers.
 *
 * It can fail on either half: fetching the item carries the #GPasteClient
 * domains (%G_PASTE_ERROR, %G_DBUS_ERROR, %G_IO_ERROR), while decoding bytes
 * GDK cannot make a texture out of yields %GDK_TEXTURE_ERROR.
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

/**
 * g_paste_gtk_util_toast:
 * @origin: the #GtkWidget the message is about
 * @message: what to say
 *
 * Say something to the user, in the window @origin is in.
 *
 * Quiet when there is nowhere to say it: our application windows wrap their
 * content in an #AdwToastOverlay, the daemon's prompt windows do not, and a
 * widget torn down while a reply was in flight is in no window at all. Failing
 * quietly is right in each case — a prompt has no history list for an empty to
 * have failed on.
 */
G_PASTE_VISIBLE void
g_paste_gtk_util_toast (GtkWidget   *origin,
                        const gchar *message)
{
    g_return_if_fail (GTK_IS_WIDGET (origin));

    GtkRoot *root = gtk_widget_get_root (origin);

    if (!ADW_IS_APPLICATION_WINDOW (root))
        return;

    GtkWidget *content = adw_application_window_get_content (ADW_APPLICATION_WINDOW (root));

    if (ADW_IS_TOAST_OVERLAY (content))
        adw_toast_overlay_add_toast (ADW_TOAST_OVERLAY (content), adw_toast_new (message));
}

typedef struct
{
    GPasteClient *client;
    gchar        *history;
    GtkWindow    *parent; /* borrowed: it outlives the dialog it put up */
} EmptyHistoryCallbackData;

static void
on_history_emptied (GObject      *source_object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
    g_autoptr (GtkWindow) parent = user_data;
    g_autoptr (GError) error = NULL;

    g_paste_client_empty_history_finish (G_PASTE_CLIENT (source_object), result, &error);

    if (!error)
        return;

    g_warning ("Could not empty the history: %s", error->message);

    /* empty_history() takes a parent it is allowed not to have -- a menu item is
     * a GObject, not a widget in a window -- and the toast helper asks for a
     * widget. Nowhere to say it is the quiet case it already documents. */
    if (parent)
        g_paste_gtk_util_toast (GTK_WIDGET (parent), _("Could not empty the history"));
}

static void
empty_history (GPasteClient *client,
               const gchar  *history,
               GtkWindow    *parent)
{
    g_paste_client_empty_history (client, history, on_history_emptied,
                                  (parent) ? g_object_ref (parent) : NULL);
}

static void
empty_history_callback (gboolean confirmed,
                        gpointer user_data)
{
    g_autofree EmptyHistoryCallbackData *data = user_data;
    g_autoptr (GPasteClient) client = data->client;
    g_autofree gchar *history = data->history;

    if (confirmed)
        empty_history (client, history, data->parent);
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
        data->parent = parent_window;

        /* Translators: %s is the name of the history being emptied. */
        g_autofree gchar *heading = g_strdup_printf (_("Empty \u201c%s\u201d?"), history);
        g_paste_gtk_util_confirm_dialog (parent_window,
                                         heading,
                                         _("Every item it holds is deleted for good."),
                                         C_("verb", "Empty"),
                                         ADW_RESPONSE_DESTRUCTIVE,
                                         empty_history_callback,
                                         data);
    }
    else
        empty_history (client, history, parent_window);
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

static void
on_form_dialog_cancel (GtkButton *button,
                       gpointer   user_data G_GNUC_UNUSED)
{
    adw_dialog_close (ADW_DIALOG (gtk_widget_get_ancestor (GTK_WIDGET (button), ADW_TYPE_DIALOG)));
}

/**
 * g_paste_gtk_util_form_dialog:
 * @heading: what the dialog is for
 * @confirm_label: the label of the confirm button
 * @content: (transfer none): what fills the dialog under its header bar
 * @width: the content width to give the dialog
 * @height: the content height to give it, or 0 to let @content ask for its own
 * @confirm: (out) (transfer none): the confirm button, which the caller connects
 *           to and is free to follow -- desensitizing it while the form is
 *           incomplete, and again once it has been pressed, since closing is
 *           asynchronous and the button stays live until it happens
 *
 * The shell every composer puts up: @content under a header bar offering to
 * cancel or to confirm, cancelling being all it handles by itself.
 *
 * A dialog rather than an alert: an alert is a short message with a question
 * attached, and a composer is neither -- a text editor six hundred pixels wide,
 * or a small form in a boxed list.
 *
 * Returns: (transfer none): the dialog, not presented yet: the caller still has
 *          its own data to hang off it
 */
G_PASTE_VISIBLE AdwDialog *
g_paste_gtk_util_form_dialog (const gchar  *heading,
                              const gchar  *confirm_label,
                              GtkWidget    *content,
                              gint          width,
                              gint          height,
                              GtkWidget   **confirm)
{
    g_return_val_if_fail (heading, NULL);
    g_return_val_if_fail (confirm_label, NULL);
    g_return_val_if_fail (GTK_IS_WIDGET (content), NULL);
    g_return_val_if_fail (confirm, NULL);

    GtkWidget *cancel = gtk_button_new_with_label (_("Cancel"));

    *confirm = gtk_button_new_with_label (confirm_label);

    gtk_widget_add_css_class (*confirm, "suggested-action");
    g_signal_connect (cancel, "clicked", G_CALLBACK (on_form_dialog_cancel), NULL);

    GtkWidget *header = adw_header_bar_new ();

    adw_header_bar_set_show_start_title_buttons (ADW_HEADER_BAR (header), FALSE);
    adw_header_bar_set_show_end_title_buttons (ADW_HEADER_BAR (header), FALSE);
    adw_header_bar_pack_start (ADW_HEADER_BAR (header), cancel);
    adw_header_bar_pack_end (ADW_HEADER_BAR (header), *confirm);

    GtkWidget *toolbar = adw_toolbar_view_new ();

    adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), header);
    adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), content);

    AdwDialog *dialog = adw_dialog_new ();

    adw_dialog_set_title (dialog, heading);
    adw_dialog_set_content_width (dialog, width);
    if (height)
        adw_dialog_set_content_height (dialog, height);
    adw_dialog_set_child (dialog, toolbar);

    return dialog;
}

/* Kept on the dialog, so it lives exactly as long as the dialog does. @answer
 * is what the user wrote, set only when they confirmed: the result is delivered
 * from "closed", which is the one place every way out of the dialog goes
 * through. */
typedef struct
{
    GPasteGtkTextDialogCallback callback;
    gpointer                    user_data;
    GtkTextBuffer              *buffer;
    gchar                      *answer;
} GPasteGtkTextDialogData;

static void
g_paste_gtk_text_dialog_data_free (gpointer user_data)
{
    GPasteGtkTextDialogData *data = user_data;

    g_free (data->answer);
    g_free (data);
}

static void
on_text_dialog_closed (AdwDialog *dialog,
                       gpointer   user_data G_GNUC_UNUSED)
{
    GPasteGtkTextDialogData *data = g_object_get_data (G_OBJECT (dialog), "text-dialog-data");

    data->callback (data->answer, data->user_data);
}

static void
on_text_dialog_confirm (GtkButton *button,
                        gpointer   user_data G_GNUC_UNUSED)
{
    AdwDialog *dialog = ADW_DIALOG (gtk_widget_get_ancestor (GTK_WIDGET (button), ADW_TYPE_DIALOG));
    GPasteGtkTextDialogData *data = g_object_get_data (G_OBJECT (dialog), "text-dialog-data");
    GtkTextIter start, end;

    gtk_text_buffer_get_bounds (data->buffer, &start, &end);
    /* Closing is what delivers the answer, and the close below is asynchronous:
     * the button is still live until it happens, so a second press would drop
     * the first answer on the floor. */
    g_free (data->answer);
    data->answer = gtk_text_buffer_get_text (data->buffer, &start, &end, FALSE);

    adw_dialog_close (dialog);
}

/**
 * g_paste_gtk_util_text_dialog:
 * @parent: (nullable): the parent #GtkWindow
 * @heading: what the dialog is for
 * @confirm_label: the label of the confirm button
 * @text: (nullable): what the text view starts with
 * @callback: (closure user_data) (scope async): handed what the user wrote,
 *            or %NULL if they cancelled
 *
 * The dialog for composing an item: a wrapping, scrollable text view in the shell
 * g_paste_gtk_util_form_dialog () puts up. Both adding a new item and editing an
 * existing one put up the same one.
 */
G_PASTE_VISIBLE void
g_paste_gtk_util_text_dialog (GtkWindow                  *parent,
                              const gchar                *heading,
                              const gchar                *confirm_label,
                              const gchar                *text,
                              GPasteGtkTextDialogCallback callback,
                              gpointer                    user_data)
{
    g_return_if_fail (!parent || GTK_IS_WINDOW (parent));
    g_return_if_fail (heading);
    g_return_if_fail (confirm_label);
    g_return_if_fail (callback);

    GtkWidget *view = gtk_text_view_new ();
    GtkTextView *tv = GTK_TEXT_VIEW (view);
    GtkWidget *scroll = gtk_scrolled_window_new ();
    GtkScrolledWindow *sw = GTK_SCROLLED_WINDOW (scroll);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (tv);

    gtk_text_view_set_wrap_mode (tv, GTK_WRAP_WORD);
    gtk_text_view_set_top_margin (tv, 6);
    gtk_text_view_set_bottom_margin (tv, 6);
    gtk_text_view_set_left_margin (tv, 6);
    gtk_text_view_set_right_margin (tv, 6);

    if (text)
        gtk_text_buffer_set_text (buffer, text, -1);

    gtk_scrolled_window_set_child (sw, view);
    gtk_widget_set_vexpand (scroll, TRUE);

    GtkWidget *confirm;
    AdwDialog *dialog = g_paste_gtk_util_form_dialog (heading, confirm_label, scroll, 600, 400, &confirm);

    g_signal_connect (confirm, "clicked", G_CALLBACK (on_text_dialog_confirm), NULL);

    GPasteGtkTextDialogData *data = g_new0 (GPasteGtkTextDialogData, 1);

    data->callback = callback;
    data->user_data = user_data;
    data->buffer = buffer;

    g_object_set_data_full (G_OBJECT (dialog), "text-dialog-data", data, g_paste_gtk_text_dialog_data_free);
    g_signal_connect (dialog, "closed", G_CALLBACK (on_text_dialog_closed), NULL);

    adw_dialog_present (dialog, GTK_WIDGET (parent));
    gtk_widget_grab_focus (view);
}
