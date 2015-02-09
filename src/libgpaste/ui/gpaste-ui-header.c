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

#include "gpaste-ui-header-private.h"

G_DEFINE_TYPE (GPasteUiHeader, g_paste_ui_header, GTK_TYPE_HEADER_BAR)

static void
g_paste_ui_header_class_init (GPasteUiHeaderClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_ui_header_init (GPasteUiHeader *self)
{
    GtkHeaderBar *header_bar = GTK_HEADER_BAR (self);

    gtk_header_bar_set_title(header_bar, PACKAGE_STRING);
    gtk_header_bar_set_show_close_button (header_bar, TRUE);
    gtk_header_bar_pack_end (header_bar, g_paste_ui_settings_new ());
}

/**
 * g_paste_ui_header_new:
 * @topwin: the main #GtkWindow
 * @client: a #GPasteClient instance
 *
 * Create a new instance of #GPasteUiHeader
 *
 * Returns: a newly allocated #GPasteUiHeader
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_header_new (GtkWindow    *topwin,
                       GPasteClient *client)
{
    g_return_val_if_fail (GTK_IS_WINDOW (topwin), NULL);
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_HEADER, NULL);
    GtkHeaderBar *bar = GTK_HEADER_BAR (self);

    gtk_header_bar_pack_end (bar, g_paste_ui_about_new (topwin));
    gtk_header_bar_pack_end (bar, g_paste_ui_empty_new (topwin, client));
    gtk_header_bar_pack_end (bar, g_paste_ui_switch_new (topwin, client));

    return self;
}
