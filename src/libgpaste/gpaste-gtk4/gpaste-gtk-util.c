/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk4/gpaste-gtk-preferences-window.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>

#define CONFIRM_DATA_KEY "gpaste-confirm-dialog-button-ok"

typedef struct {
    GtkWindow                     *dialog;
    GPasteGtkConfirmDialogCallback callback;
    gpointer                       user_data;
} GPasteGtkConfirmDialogCallbackData;

static void
on_confirm_button_clicked (GtkButton *button,
                           gpointer   user_data)
{
    g_autofree GPasteGtkConfirmDialogCallbackData *data = user_data;

    gboolean ok = !!g_object_get_data (G_OBJECT (button), CONFIRM_DATA_KEY);

    gtk_window_destroy (GTK_WINDOW (data->dialog));
    data->callback (ok, data->user_data);
}

static GtkWidget *
confirm_button (const gchar                        *action,
		gboolean                            ok,
                GPasteGtkConfirmDialogCallbackData *data)
{
    GtkWidget *button = gtk_button_new_with_label (action);

    gtk_button_set_use_underline (GTK_BUTTON (button), TRUE);
    gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
    g_object_set_data (G_OBJECT (button), CONFIRM_DATA_KEY, GSIZE_TO_POINTER (ok));

    g_signal_connect (G_OBJECT (button),
                      "clicked",
                      G_CALLBACK (on_confirm_button_clicked),
                      data);

    return button;
}

static GtkWidget *
confirm_header_bar (const gchar                        *action,
                    GPasteGtkConfirmDialogCallbackData *data)
{
    GtkWidget *header_bar = gtk_header_bar_new ();
    GtkHeaderBar *header = GTK_HEADER_BAR (header_bar);

    gtk_header_bar_set_show_title_buttons (header, FALSE);
    gtk_header_bar_pack_start (header, confirm_button (_("Cancel"), FALSE, data));
    gtk_header_bar_pack_end (header, confirm_button (action, TRUE, data));

    return header_bar;
}

static GtkWidget *
confirm_label (const gchar *msg)
{
    GtkWidget *label = gtk_label_new (msg);

    gtk_widget_set_vexpand (label, TRUE);
    gtk_widget_set_valign (label, TRUE);
    gtk_widget_set_visible (label, TRUE);

    return label;
}

static GtkWidget *
confirm_dialog_window (const gchar                   *action,
                       GPasteGtkConfirmDialogCallback on_confirmation,
                       gpointer                       user_data)
{
    GtkWidget *dialog = gtk_window_new ();
    GtkWindow *win = GTK_WINDOW (dialog);
    GPasteGtkConfirmDialogCallbackData *data = g_new (GPasteGtkConfirmDialogCallbackData, 1);

    data->dialog = win;
    data->callback = on_confirmation;
    data->user_data = user_data;

    gtk_window_set_title (win, PACKAGE_STRING);
    gtk_window_set_titlebar (win, confirm_header_bar (action, data));
    gtk_window_set_modal (win, TRUE);
    gtk_window_set_destroy_with_parent (win, TRUE);

    return dialog;
}

/**
 * g_paste_gtk_util_confirm_dialog:
 * @parent: (nullable): the parent #GtkWindow
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

    GtkWidget *dialog = confirm_dialog_window (action, on_confirmation, user_data);
    GtkWindow *win = GTK_WINDOW (dialog);

    if (parent)
	gtk_window_set_transient_for(win, parent);

    gtk_widget_set_parent (confirm_label (msg), dialog);
    gtk_window_present (win);
}

/**
 * g_paste_gtk_util_compute_checksum:
 * @texture: the #GdkTexture to checksum
 *
 * Compute the checksum of an image
 *
 * Returns: the newly allocated checksum
 */
G_PASTE_VISIBLE gchar *
g_paste_gtk_util_compute_checksum (GdkTexture *texture)
{
    if (!texture || !GDK_IS_TEXTURE (texture))
        return NULL;

    cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                                           gdk_texture_get_width (texture),
                                                           gdk_texture_get_height (texture));

    gdk_texture_download (texture,
                          cairo_image_surface_get_data (surface),
                          cairo_image_surface_get_stride (surface));
    cairo_surface_mark_dirty (surface);

    guint8 *data = cairo_image_surface_get_data (surface);
    gsize length = cairo_image_surface_get_height (surface) * cairo_image_surface_get_stride (surface);
    gchar *checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA256, data, length);

    cairo_surface_destroy (surface);

    return checksum;
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

        /* Translators: this is the translation for emptying the history */
        g_paste_gtk_util_confirm_dialog (parent_window, _("Empty"), _("Do you really want to empty the history?"), empty_history_callback, data);
    }
    else
    {
        g_paste_client_empty_history (client, history, NULL, NULL);
    }
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
