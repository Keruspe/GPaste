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
    GtkWindow    *topwin;
    GPasteClient *client;

    gulong        active_id;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiSwitch, g_paste_ui_switch, GTK_TYPE_SWITCH)

static void
on_active_changed (GObject *gobject)
{
    GPasteUiSwitchPrivate *priv = g_paste_ui_switch_get_instance_private ((GPasteUiSwitch *) gobject);
    GtkSwitch *sw = GTK_SWITCH (gobject);
    gboolean track = gtk_switch_get_active (sw);
    gboolean changed = TRUE;

    if (!track)
    {
        changed = g_paste_util_confirm_dialog (priv->topwin, _("Do you really want to stop tracking clipboard changes?"));
        track = !changed;
    }

    if (changed)
        g_paste_client_track (priv->client, track, NULL, NULL);
    else
        gtk_switch_set_active (sw, TRUE); /* FIXME: infinite loop */
}

static void
g_paste_ui_switch_dispose (GObject *object)
{
    GPasteUiSwitchPrivate *priv = g_paste_ui_switch_get_instance_private ((GPasteUiSwitch *) object);

    g_clear_object (&priv->client);

    if (priv->active_id)
    {
        g_signal_handler_disconnect (object, priv->active_id);
        priv->active_id = 0;
    }

    G_OBJECT_CLASS (g_paste_ui_switch_parent_class)->dispose (object);
}

static void
g_paste_ui_switch_class_init (GPasteUiSwitchClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_switch_dispose;
}

static void
g_paste_ui_switch_init (GPasteUiSwitch *self)
{
    GPasteUiSwitchPrivate *priv = g_paste_ui_switch_get_instance_private ((GPasteUiSwitch *) self);

    gtk_widget_set_tooltip_text (GTK_WIDGET (self), _("Track clipboard changes"));

    priv->active_id = g_signal_connect (G_OBJECT (self),
                                        "notify::active",
                                        G_CALLBACK (on_active_changed),
                                        NULL);
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
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL); /* FIXME: track state changes */

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_SWITCH, NULL);
    GPasteUiSwitchPrivate *priv = g_paste_ui_switch_get_instance_private ((GPasteUiSwitch *) self);

    priv->topwin = topwin;
    priv->client = g_object_ref (client);

    return self;
}
