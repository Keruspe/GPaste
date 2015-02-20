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

#include <gpaste-ui-header.h>
#include <gpaste-ui-history.h>

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
g_paste_ui_window_init (GPasteUiWindow *self G_GNUC_UNUSED)
{
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
                                      "window-position", GTK_WIN_POS_CENTER_ALWAYS,
                                      "resizable",       FALSE,
                                      "width-request",   800,
                                      "height-request",  600,
                                      NULL);
    GtkWindow *win = GTK_WINDOW (self);
    GPasteUiWindowPrivate *priv = g_paste_ui_window_get_instance_private ((GPasteUiWindow *) self);

    priv->app = app;

    gtk_window_set_titlebar (win, g_paste_ui_header_new (win, client));
    gtk_container_add (GTK_CONTAINER (self), g_paste_ui_history_new (client));

    return self;
}
