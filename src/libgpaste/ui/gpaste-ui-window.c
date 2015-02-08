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

#include "gpaste-ui-window-private.h"

struct _GPasteUiWindowPrivate
{
    GtkApplication *app;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiWindow, g_paste_ui_window, GTK_TYPE_WINDOW)

static void
g_paste_ui_window_class_init (GPasteUiWindowClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_ui_window_init (GPasteUiWindow *self)
{
    GtkWidget *bar = gtk_header_bar_new ();
    GtkHeaderBar *header_bar = GTK_HEADER_BAR (bar);

    gtk_header_bar_set_title(header_bar, PACKAGE_STRING);
    gtk_header_bar_set_show_close_button (header_bar, TRUE);
    gtk_window_set_titlebar (GTK_WINDOW (self), bar);
}

/**
 * g_paste_ui_window_new:
 * @app: the #GtkApplication
 * @client: a #GPasteClient instance
 *
 * Create a new instance of #GPasteUiWindow
 *
 * Returns: a newly allocated #GPasteUiWindow
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_window_new (GtkApplication *app,
                       GPasteClient   *client)
{
    g_return_val_if_fail (GTK_IS_APPLICATION (app), NULL);
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_WINDOW,
                                      "application",     app,
                                      "type",            GTK_WINDOW_TOPLEVEL,
                                      "window-position", GTK_WIN_POS_CENTER,
                                      "resizable",       FALSE,
                                      NULL);
    GPasteUiWindowPrivate *priv = g_paste_ui_window_get_instance_private ((GPasteUiWindow *) self);

    priv->app = app;

    gtk_header_bar_pack_end (GTK_HEADER_BAR (gtk_window_get_titlebar (GTK_WINDOW (self))), g_paste_ui_settings_new ());
    gtk_container_add (GTK_CONTAINER (self), g_paste_ui_history_new (client));

    return self;
}
