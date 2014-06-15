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

#include "gpaste-applet-icon-private.h"

struct _GPasteAppletIconPrivate
{
    GPasteClient *client;

    GtkMenu      *menu;

    gulong         show_id;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GPasteAppletIcon, g_paste_applet_icon, G_TYPE_OBJECT)

/**
 * g_paste_applet_icon_popup: (skip)
 */
gboolean
g_paste_applet_icon_popup (GPasteAppletIcon   *self,
                           GdkEvent           *event,
                           GtkMenuPositionFunc func,
                           gpointer            data)
{
    GPasteAppletIconPrivate *priv = g_paste_applet_icon_get_instance_private (self);
    GtkWidget *widget = GTK_WIDGET (priv->menu);
    guint button;

    if (!event || !gdk_event_get_button (event, &button))
        button = 0;

    gtk_widget_set_visible (widget, TRUE);
    gtk_widget_show (gtk_widget_get_toplevel (widget));
    gtk_menu_popup (priv->menu, NULL, NULL, func, data, button, gdk_event_get_time (event));

    return FALSE;
}

static void
g_paste_applet_icon_show_history (GPasteClient *client G_GNUC_UNUSED,
                                  gpointer      user_data)
{
    GPasteAppletIcon *self = user_data;
    GPasteAppletIconClass *klass = G_PASTE_APPLET_ICON_GET_CLASS (self);

    if (klass->popup)
        klass->popup (self, gtk_get_current_event ());
}

static void
g_paste_applet_icon_dispose (GObject *object)
{
    GPasteAppletIconPrivate *priv = g_paste_applet_icon_get_instance_private ((GPasteAppletIcon *) object);

    if (priv->client)
    {
        g_signal_handler_disconnect (priv->client, priv->show_id);
        g_clear_object (&priv->client);
    }

    G_OBJECT_CLASS (g_paste_applet_icon_parent_class)->dispose (object);
}

static void
g_paste_applet_icon_class_init (GPasteAppletIconClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_applet_icon_dispose;
    klass->popup = NULL;
}

static void
g_paste_applet_icon_init (GPasteAppletIcon *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_applet_icon_new: (skip)
 */
GPasteAppletIcon *
g_paste_applet_icon_new (GType         type,
                         GPasteClient *client,
                         GtkMenu      *menu)
{
    g_return_val_if_fail (g_type_is_a (type, G_PASTE_TYPE_APPLET_ICON), NULL);
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (GTK_IS_MENU (menu), NULL);

    GPasteAppletIcon *self = g_object_new (type, NULL);
    GPasteAppletIconPrivate *priv = g_paste_applet_icon_get_instance_private (self);

    priv->client = g_object_ref (client);
    priv->menu = menu;

    priv->show_id = g_signal_connect (client,
                                      "show-history",
                                      G_CALLBACK (g_paste_applet_icon_show_history),
                                      self);

    return self;
}
