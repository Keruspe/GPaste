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

#include "gpaste-applet-switch-private.h"

#include <glib/gi18n-lib.h>

struct _GPasteAppletSwitchPrivate
{
    GPasteClient *client;
    GtkSwitch    *sw;

    gulong        tracking_id;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteAppletSwitch, g_paste_applet_switch, GTK_TYPE_MENU_ITEM)

/**
 * g_paste_applet_switch_get_active:
 * @self: a #GPasteAppletSwitch instance
 *
 * Gets whether the switch is in its "on" or "off" state.
 *
 * Returns: TRUE if the switch is active, and FALSE otherwise
 */
G_PASTE_VISIBLE gboolean
g_paste_applet_switch_get_active (const GPasteAppletSwitch *self)
{
    g_return_val_if_fail (G_PASTE_IS_APPLET_SWITCH (self), FALSE);

    GPasteAppletSwitchPrivate *priv = g_paste_applet_switch_get_instance_private ((GPasteAppletSwitch *) self);
    return gtk_switch_get_active (priv->sw);
}

/**
 * g_paste_applet_switch_set_active:
 * @self: a #GPasteAppletSwitch instance
 * @active: TRUE if the switch should be active, and FALSE otherwise
 *
 * Changes the state of the switch to the desired one.
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_applet_switch_set_active (GPasteAppletSwitch *self,
                                  gboolean            active)
{
    g_return_if_fail (G_PASTE_IS_APPLET_SWITCH (self));

    GPasteAppletSwitchPrivate *priv = g_paste_applet_switch_get_instance_private (self);

    if (active == gtk_switch_get_active (priv->sw))
        return;

    g_paste_client_track (priv->client, active, NULL, NULL);
}

static void
g_paste_applet_switch_private_on_tracking (GPasteClient *client G_GNUC_UNUSED,
                                           gboolean      state,
                                           gpointer      user_data)
{
    GtkSwitch *sw = user_data;
    gtk_switch_set_active (sw, state);
}

static gboolean
g_paste_applet_switch_button_release_event (GtkWidget      *widget,
                                            GdkEventButton *event G_GNUC_UNUSED)
{
    GPasteAppletSwitch *self = (GPasteAppletSwitch *) widget;
    g_paste_applet_switch_set_active (self, !g_paste_applet_switch_get_active (self));
    return TRUE;
}

static void
g_paste_applet_switch_dispose (GObject *object)
{
    GPasteAppletSwitchPrivate *priv = g_paste_applet_switch_get_instance_private ((GPasteAppletSwitch *) object);

    if (priv->tracking_id)
    {
        g_signal_handler_disconnect (priv->client, priv->tracking_id);
        priv->tracking_id = 0;
    }
    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_applet_switch_parent_class)->dispose (object);
}

static void
g_paste_applet_switch_class_init (GPasteAppletSwitchClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_applet_switch_dispose;
    GTK_WIDGET_CLASS (klass)->button_release_event = g_paste_applet_switch_button_release_event;
}

static void
g_paste_applet_switch_init (GPasteAppletSwitch *self)
{
    GPasteAppletSwitchPrivate *priv = g_paste_applet_switch_get_instance_private (self);

    GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
    GtkBox *box = GTK_BOX (hbox);
    gtk_box_pack_start (box, gtk_label_new (_("Track changes")), FALSE, FALSE, 0);

    GtkWidget *sw = gtk_switch_new ();
    priv->sw = GTK_SWITCH (sw);
    gtk_box_pack_end (box, sw, FALSE, TRUE, 0);

    gtk_container_add (GTK_CONTAINER (self), hbox);

    priv->tracking_id = 0;
}

/**
 * g_paste_applet_switch_new:
 * @client: a #GPasteClient instance
 *
 * Create a new instance of #GPasteAppletSwitch
 *
 * Returns: a newly allocated #GPasteAppletSwitch
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_applet_switch_new (GPasteClient *client)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_APPLET_SWITCH, NULL);
    GPasteAppletSwitchPrivate *priv = g_paste_applet_switch_get_instance_private ((GPasteAppletSwitch *) self);

    priv->client = g_object_ref (client);
    g_paste_applet_switch_set_active (G_PASTE_APPLET_SWITCH (self), g_paste_client_is_active (client));

    priv->tracking_id = g_signal_connect (G_OBJECT (client),
                                          "tracking",
                                          G_CALLBACK (g_paste_applet_switch_private_on_tracking),
                                          priv->sw);

    return self;
}
