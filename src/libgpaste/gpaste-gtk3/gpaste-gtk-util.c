/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk3/gpaste-gtk-util.h>

/**
 * g_paste_gtk_util_confirm_dialog:
 * @parent: (nullable): the parent #GtkWindow
 * @msg: the message to display
 *
 * Ask the user for confirmation
 */
G_PASTE_VISIBLE gboolean
g_paste_gtk_util_confirm_dialog (GtkWindow   *parent,
                                 const gchar *action,
                                 const gchar *msg)
{
    g_return_val_if_fail (!parent || GTK_IS_WINDOW (parent), FALSE);
    g_return_val_if_fail (action, FALSE);
    g_return_val_if_fail (g_utf8_validate (msg, -1, NULL), FALSE);

    GtkWidget *dialog = gtk_dialog_new_with_buttons (PACKAGE_STRING, parent,
                                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_USE_HEADER_BAR,
                                                     action,      GTK_RESPONSE_OK,
                                                     _("Cancel"), GTK_RESPONSE_CANCEL,
                                                     NULL);
    GtkWidget *label = gtk_label_new (msg);
    GtkDialog *d = GTK_DIALOG (dialog);

    gtk_widget_set_vexpand (label, TRUE);
    gtk_widget_set_valign (label, TRUE);
    gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (d)), label, TRUE, TRUE, 0);
    gtk_widget_show (label);

    gboolean ret = gtk_dialog_run (d) == GTK_RESPONSE_OK;

    gtk_widget_destroy (dialog);

    return ret;
}

/**
 * g_paste_gtk_util_compute_checksum:
 * @image: the #GdkPixbuf to checksum
 *
 * Compute the checksum of an image
 *
 * Returns: the newly allocated checksum
 */
G_PASTE_VISIBLE gchar *
g_paste_gtk_util_compute_checksum (GdkPixbuf *image)
{
    if (!image || !GDK_IS_PIXBUF (image))
        return NULL;

    const guint8 *data = gdk_pixbuf_read_pixels (image);
    gsize length = gdk_pixbuf_get_byte_length (image);

    return g_compute_checksum_for_data (G_CHECKSUM_SHA256, data, length);
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
    if (!g_paste_settings_get_empty_history_confirmation (settings) ||
        /* Translators: this is the translation for emptying the history */
        g_paste_gtk_util_confirm_dialog (parent_window, _("Empty"), _("Do you really want to empty the history?")))
            g_paste_client_empty_history (client, history, NULL, NULL);
}

/**
 * g_paste_gtk_util_show_win:
 * @application: a #GtkApplication
 *
 * Present the application's window to user
 */
G_PASTE_VISIBLE void
g_paste_gtk_util_show_win (GApplication *application)
{
    g_return_if_fail (GTK_IS_APPLICATION (application));

    for (GList *wins = gtk_application_get_windows (GTK_APPLICATION (application)); wins; wins = g_list_next (wins))
    {
        if (GTK_IS_WIDGET (wins->data) && gtk_widget_get_realized (wins->data))
            gtk_window_present (wins->data);
    }
}
