/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-applet-quit-private.h"

#include <glib/gi18n-lib.h>

struct _GPasteAppletQuitPrivate
{
    GApplication *app;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteAppletQuit, g_paste_applet_quit, GTK_TYPE_MENU_ITEM)

static void
g_paste_applet_quit_activate (GtkMenuItem *menu_item)
{
    GPasteAppletQuitPrivate *priv = g_paste_applet_quit_get_instance_private ((GPasteAppletQuit *) menu_item);

    g_application_quit (priv->app);

    GTK_MENU_ITEM_CLASS (g_paste_applet_quit_parent_class)->activate (menu_item);
}

static void
g_paste_applet_quit_class_init (GPasteAppletQuitClass *klass)
{
    GTK_MENU_ITEM_CLASS (klass)->activate = g_paste_applet_quit_activate;
}

static void
g_paste_applet_quit_init (GPasteAppletQuit *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_applet_quit_new:
 * @app: the #GApplication
 *
 * Create a new instance of #GPasteAppletQuit
 *
 * Returns: a newly allocated #GPasteAppletQuit
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_applet_quit_new (GApplication *app)
{
    g_return_val_if_fail (G_IS_APPLICATION (app), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_APPLET_QUIT,
                                      "label", _("Quit"),
                                      NULL);
    GPasteAppletQuitPrivate *priv = g_paste_applet_quit_get_instance_private ((GPasteAppletQuit *) self);
    priv->app = app;
    return self;
}
