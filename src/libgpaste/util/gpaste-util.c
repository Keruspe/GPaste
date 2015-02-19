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

#include "gpaste-util.h"

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
    const gchar *_authors[] = {
        "Marc-Antoine Perennou <Marc-Antoine@Perennou.com>",
        NULL
    };
    G_PASTE_CLEANUP_B_STRV_FREE GStrv authors = g_boxed_copy (G_TYPE_STRV, _authors);
    gtk_show_about_dialog (parent,
                           "program-name",   PACKAGE_NAME,
                           "version",        PACKAGE_VERSION,
                           "logo-icon-name", "gtk-paste",
                           "license",        LICENSE,
                           "authors",        authors,
                           "copyright",      "Copyright © 2010-2014 Marc-Antoine Perennou",
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

/**
 * g_paste_util_spawn:
 * @app: the GPaste app to spawn
 * @error: a #GError or %NULL
 *
 * spawn a GPaste app
 *
 * Returns: whether the spawn was successful
 */
G_PASTE_VISIBLE gboolean
g_paste_util_spawn (const gchar *app,
                    GError     **error)
{
    G_PASTE_CLEANUP_FREE gchar *name = g_strdup_printf ("org.gnome.GPaste.%s", app);
    G_PASTE_CLEANUP_FREE gchar *object = g_strdup_printf ("/org/gnome/GPaste/%s", app);
    G_PASTE_CLEANUP_UNREF GDBusProxy *proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
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

    g_dbus_proxy_call_sync (proxy, "Activate", params, G_DBUS_CALL_FLAGS_NONE, -1, NULL, error);

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
    G_PASTE_CLEANUP_FREE gchar *regex_string = g_regex_escape_string (pattern, -1);
    G_PASTE_CLEANUP_REGEX_UNREF GRegex *regex = g_regex_new (regex_string,
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

