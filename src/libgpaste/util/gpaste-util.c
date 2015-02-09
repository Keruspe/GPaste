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

