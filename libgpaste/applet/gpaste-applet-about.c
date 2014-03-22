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

#include "gpaste-applet-about-private.h"

#include <glib/gi18n-lib.h>

struct _GPasteAppletAboutPrivate
{
    GPasteClient *client;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteAppletAbout, g_paste_applet_about, GTK_TYPE_MENU_ITEM)

static void
g_paste_applet_about_activate (GtkMenuItem *menu_item)
{
    GPasteAppletAboutPrivate *priv = g_paste_applet_about_get_instance_private ((GPasteAppletAbout *) menu_item);

    g_paste_client_about (priv->client, NULL, NULL);

    GTK_MENU_ITEM_CLASS (g_paste_applet_about_parent_class)->activate (menu_item);
}

static void
g_paste_applet_about_dispose (GObject *object)
{
    GPasteAppletAboutPrivate *priv = g_paste_applet_about_get_instance_private ((GPasteAppletAbout *) object);

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_applet_about_parent_class)->dispose (object);
}

static void
g_paste_applet_about_class_init (GPasteAppletAboutClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_applet_about_dispose;
    GTK_MENU_ITEM_CLASS (klass)->activate = g_paste_applet_about_activate;
}

static void
g_paste_applet_about_init (GPasteAppletAbout *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_applet_about_new:
 * @client: a #GPasteClient instance
 *
 * Create a new instance of #GPasteAppletAbout
 *
 * Returns: a newly allocated #GPasteAppletAbout
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_applet_about_new (GPasteClient *client)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_APPLET_ABOUT,
                                      "label", _("About"),
                                      NULL);
    GPasteAppletAboutPrivate *priv = g_paste_applet_about_get_instance_private ((GPasteAppletAbout *) self);
    priv->client = g_object_ref (client);
    return self;
}
