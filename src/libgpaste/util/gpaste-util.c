/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gpaste-macros.h>
#include <gpaste-util.h>

#define LICENSE                                                            \
    "GPaste is free software: you can redistribute it and/or modify"       \
    "it under the terms of the GNU General Public License as published by" \
    "the Free Software Foundation, either version 3 of the License, or"    \
    "(at your option) any later version.\n\n"                              \
    "GPaste is distributed in the hope that it will be useful,"            \
    "but WITHOUT ANY WARRANTY; without even the implied warranty of"       \
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the"        \
    "GNU General Public License for more details.\n\n"                     \
    "You should have received a copy of the GNU General Public License"    \
    "along with GPaste.  If not, see <http://www.gnu.org/licenses/>."

/**
 * g_paste_util_show_about_dialog:
 * @parent: the parent #GtkWindow
 *
 * Show GPaste about dialog
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_util_show_about_dialog (GtkWindow *parent)
{
    const gchar *authors[] = {
        "Marc-Antoine Perennou <Marc-Antoine@Perennou.com>",
        NULL
    };
    gtk_show_about_dialog (parent,
                           "program-name",   PACKAGE_NAME,
                           "version",        PACKAGE_VERSION,
                           "logo-icon-name", "gtk-paste",
                           "license",        LICENSE,
                           "authors",        authors,
                           "copyright",      "Copyright © 2010-2015 Marc-Antoine Perennou",
                           "comments",       "Clipboard management system",
                           "website",        "http://www.imagination-land.org/tags/GPaste.html",
                           "website-label",  "Follow GPaste news",
                           "wrap-license",   TRUE,
                           NULL);
}

/**
 * g_paste_util_confirm_dialog:
 * @parent: the parent #GtkWindow
 * @msg: the message to display
 *
 * Show GPaste about dialog
 *
 * Returns:
 */
G_PASTE_VISIBLE gboolean
g_paste_util_confirm_dialog (GtkWindow   *parent,
                             const gchar *msg)
{
    GtkWidget *dialog = gtk_message_dialog_new (parent,
                                                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_USE_HEADER_BAR,
                                                GTK_MESSAGE_QUESTION,
                                                GTK_BUTTONS_OK_CANCEL,
                                                "%s", msg);
    gboolean ret = gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK;

    gtk_widget_destroy (dialog);

    return ret;
}

static void
g_paste_util_spawn_on_proxy_ready (GObject      *source_object G_GNUC_UNUSED,
                                   GAsyncResult *res,
                                   gpointer      user_data)
{
    g_autofree gchar *cmd = user_data;
    g_autoptr (GDBusProxy) proxy = g_dbus_proxy_new_for_bus_finish (res, NULL /* error */);

    if (proxy)
    {
        GVariant *param = g_variant_new ("a{sv}", NULL);
        GVariant *params = g_variant_new_tuple (&param, 1);

        g_dbus_proxy_call (proxy, "Activate", params, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    }
}

/**
 * g_paste_util_spawn:
 * @app: the GPaste app to spawn
 *
 * spawn a GPaste app
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_util_spawn (const gchar *app)
{
    g_autofree gchar *name = g_strdup_printf ("org.gnome.GPaste.%s", app);
    g_autofree gchar *object = g_strdup_printf ("/org/gnome/GPaste/%s", app);

    g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                              G_DBUS_PROXY_FLAGS_NONE,
                              NULL,
                              name,
                              object,
                              "org.freedesktop.Application",
                              NULL,
                              g_paste_util_spawn_on_proxy_ready,
                              g_strdup (app));
}

/**
 * g_paste_util_spawn_sync:
 * @app: the GPaste app to spawn
 * @error: a #GError or %NULL
 *
 * spawn a GPaste app
 *
 * Returns: whether the spawn was successful
 */
G_PASTE_VISIBLE gboolean
g_paste_util_spawn_sync (const gchar *app,
                         GError     **error)
{
    g_autofree gchar *name = g_strdup_printf ("org.gnome.GPaste.%s", app);
    g_autofree gchar *object = g_strdup_printf ("/org/gnome/GPaste/%s", app);
    g_autoptr (GDBusProxy) proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                                  NULL,
                                                                  name,
                                                                  object,
                                                                  "org.freedesktop.Application",
                                                                  NULL,
                                                                  error);

    if (!proxy)
        return FALSE;

    GVariant *param = g_variant_new ("a{sv}", NULL);
    GVariant *params = g_variant_new_tuple (&param, 1);

    /* We only consume it */
    G_GNUC_UNUSED g_autoptr (GVariant) res = g_dbus_proxy_call_sync (proxy, "Activate", params, G_DBUS_CALL_FLAGS_NONE, -1, NULL, error);

    return TRUE;
}

/**
 * g_paste_util_relace:
 * @text: the initial text
 * @pattern: the pattern to replace
 * @substitution: the replacement text
 *
 * Replace some text
 *
 * Returns: the newly allocated string
 */
G_PASTE_VISIBLE gchar *
g_paste_util_replace (const gchar *text,
                      const gchar *pattern,
                      const gchar *substitution)
{
    g_autofree gchar *regex_string = g_regex_escape_string (pattern, -1);
    g_autoptr (GRegex) regex = g_regex_new (regex_string,
                                            0, /* Compile options */
                                            0, /* Match options */
                                            NULL); /* Error */
    return g_regex_replace_literal (regex,
                                    text,
                                    (gssize) -1,
                                    0, /* Start position */
                                    substitution,
                                    0, /* Match options */
                                    NULL); /* Error */
}

/**
 * g_paste_util_compute_checksum:
 * @image: the #GdkPixbuf to checksum
 *
 * Compute the checksum of an image
 *
 * Returns: the newly allocated checksum
 */
G_PASTE_VISIBLE gchar *
g_paste_util_compute_checksum (GdkPixbuf *image)
{
    if (!image)
        return NULL;

    guint length;
    const guchar *data = gdk_pixbuf_get_pixels_with_length (image,
                                                            &length);
    return g_compute_checksum_for_data (G_CHECKSUM_SHA256,
                                        data,
                                        length);
}

/**
 * g_paste_util_has_applet:
 *
 * Check whether gpaste-applet is installed or not
 *
 * Returns: %TRUE if gpaste-applet is installed
 */
G_PASTE_VISIBLE gboolean
g_paste_util_has_applet (void)
{
    return g_file_test (PKGLIBEXECDIR "/gpaste-applet", G_FILE_TEST_IS_EXECUTABLE);
}

/**
 * g_paste_util_has_unity:
 *
 * Check whether gpaste-app-indicator is installed or not
 *
 * Returns: %TRUE if gpaste-app-indicator is installed
 */
G_PASTE_VISIBLE gboolean
g_paste_util_has_unity (void)
{
    return g_file_test (PKGLIBEXECDIR "/gpaste-app-indicator", G_FILE_TEST_IS_EXECUTABLE);
}

/**
 * g_paste_util_show_win:
 * @application: a #GtkApplication
 *
 * Present the application's window to user
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_util_show_win (GApplication *application)
{
    for (GList *wins = gtk_application_get_windows (GTK_APPLICATION (application)); wins; wins = g_list_next (wins))
    {
        if (gtk_widget_get_realized (wins->data))
            gtk_window_present (wins->data);
    }
}

