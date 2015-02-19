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

#include "gpaste-ui-switch-private.h"

#include <glib/gi18n-lib.h>

struct _GPasteUiSwitchPrivate
{
    GPasteClient *client;

    GtkWindow    *topwin;

    gulong        tracking_id;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiSwitch, g_paste_ui_switch, GTK_TYPE_SWITCH)

static void
on_tracking_changed (GPasteClient *client G_GNUC_UNUSED,
                     gboolean      state,
                     gpointer      user_data)
{
    GtkSwitch *sw = user_data;

    gtk_switch_set_active (sw, state);
}

static gboolean
g_paste_ui_button_press_event (GtkWidget      *widget,
                               GdkEventButton *event G_GNUC_UNUSED)
{
    GPasteUiSwitchPrivate *priv = g_paste_ui_switch_get_instance_private ((GPasteUiSwitch *) widget);
    GtkSwitch *sw = GTK_SWITCH (widget);
    gboolean track = !gtk_switch_get_active (sw);
    gboolean changed = TRUE;

    if (!track)
    {
        changed = g_paste_util_confirm_dialog (priv->topwin, _("Do you really want to stop tracking clipboard changes?"));
        track = !changed;
    }

    if (changed)
        g_paste_client_track (priv->client, track, NULL, NULL);

    return GDK_EVENT_STOP;
}

static void
g_paste_ui_switch_dispose (GObject *object)
{
    GPasteUiSwitchPrivate *priv = g_paste_ui_switch_get_instance_private ((GPasteUiSwitch *) object);

    if (priv->tracking_id)
    {
        g_signal_handler_disconnect (priv->client, priv->tracking_id);
        priv->tracking_id = 0;
    }

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_switch_parent_class)->dispose (object);
}

static void
g_paste_ui_switch_class_init (GPasteUiSwitchClass *klass)
{
    GTK_WIDGET_CLASS (klass)->button_press_event = g_paste_ui_button_press_event;
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_switch_dispose;
}

static void
g_paste_ui_switch_init (GPasteUiSwitch *self)
{
    gtk_widget_set_tooltip_text (GTK_WIDGET (self), _("Track clipboard changes"));
}

/**
 * g_paste_ui_switch_new:
 * @topwin: the main #GtkWindow
 * @client: a #GPasteClient instance
 *
 * Create a new instance of #GPasteUiSwitch
 *
 * Returns: a newly allocated #GPasteUiSwitch
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_switch_new (GtkWindow    *topwin,
                       GPasteClient *client)
{
    g_return_val_if_fail (GTK_IS_WINDOW (topwin), NULL);
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_SWITCH, NULL);
    GPasteUiSwitchPrivate *priv = g_paste_ui_switch_get_instance_private ((GPasteUiSwitch *) self);

    priv->topwin = topwin;
    priv->client = g_object_ref (client);

    priv->tracking_id = g_signal_connect (G_OBJECT (priv->client),
                                          "tracking",
                                          G_CALLBACK (on_tracking_changed),
                                          self);

    gtk_switch_set_active (GTK_SWITCH (self), g_paste_client_is_active (client));

    return self;
}
