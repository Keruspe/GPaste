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

#include "gpaste-applet-private.h"

struct _GPasteAppletPrivate
{
    GPasteClient        *client;

    GPasteAppletMenu    *menu;
    GPasteAppletHistory *history;
    GPasteAppletIcon    *icon; 
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteApplet, g_paste_applet, G_TYPE_OBJECT)

/**
 * g_paste_applet_get_active:
 * @self: a #GPasteApplet instance
 *
 * Gets whether the switch is in its "on" or "off" state.
 *
 * Returns: TRUE if the switch is active, and FALSE otherwise
 */
G_PASTE_VISIBLE gboolean
g_paste_applet_get_active (const GPasteApplet *self)
{
    g_return_val_if_fail (G_PASTE_IS_APPLET (self), FALSE);

    GPasteAppletPrivate *priv = g_paste_applet_get_instance_private ((GPasteApplet *) self);
    return g_paste_applet_menu_get_active (priv->menu);
}

/**
 * g_paste_applet_set_active:
 * @self: a #GPasteApplet instance
 * @active: TRUE if the switch should be active, and FALSE otherwise
 *
 * Changes the state of the switch to the desired one.
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_applet_set_active (GPasteApplet *self,
                           gboolean      active)
{
    g_return_if_fail (G_PASTE_IS_APPLET (self));

    GPasteAppletPrivate *priv = g_paste_applet_get_instance_private (self);
    g_paste_applet_menu_set_active (priv->menu, active);
}

static void
g_paste_applet_dispose (GObject *object)
{
    GPasteAppletPrivate *priv = g_paste_applet_get_instance_private ((GPasteApplet *) object);

    g_clear_object (&priv->client);
    g_clear_object (&priv->history);
    g_clear_object (&priv->icon);

    G_OBJECT_CLASS (g_paste_applet_parent_class)->dispose (object);
}

static void
g_paste_applet_class_init (GPasteAppletClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_applet_dispose;
}

static void
g_paste_applet_init (GPasteApplet *self)
{
    GPasteAppletPrivate *priv = g_paste_applet_get_instance_private (self);
    priv->client = g_paste_client_new_sync (NULL);
}

static GPasteApplet *
g_paste_applet_new (GtkApplication *application)
{
    GPasteApplet *self = g_object_new (G_PASTE_TYPE_APPLET, NULL);
    GPasteAppletPrivate *priv = g_paste_applet_get_instance_private (self);

    priv->menu = g_paste_applet_menu_new (priv->client, G_APPLICATION (application));
    priv->history = g_paste_applet_history_new_sync (priv->client, priv->menu);

    gtk_widget_hide (gtk_application_window_new (application));

    return self;
}

/**
 * g_paste_applet_new_status_icon:
 * @application: the #GtkApplication running
 *
 * Create a new instance of #GPasteApplet
 *
 * Returns: a newly allocated #GPasteApplet
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteApplet *
g_paste_applet_new_status_icon (GtkApplication *application)
{
    g_return_val_if_fail (G_IS_APPLICATION (application), NULL);

    GPasteApplet *self = g_paste_applet_new (application);
    GPasteAppletPrivate *priv = g_paste_applet_get_instance_private (self);

    priv->icon = g_paste_applet_status_icon_new (priv->client, GTK_MENU (priv->menu));

    return self;
}
