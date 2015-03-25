/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include <gpaste-util.h>

typedef struct
{
    GPasteClient *client;

    gulong         show_id;
} GPasteAppletIconPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GPasteAppletIcon, g_paste_applet_icon, G_TYPE_OBJECT)

/*
 * g_paste_applet_icon_activate: (skip)
 */
void
g_paste_applet_icon_activate (void)
{
    g_paste_util_spawn ("Ui");
}

static void
g_paste_applet_icon_show_history (GPasteClient *client G_GNUC_UNUSED,
                                  gpointer      user_data G_GNUC_UNUSED)
{
    g_paste_applet_icon_activate ();
}

static void
g_paste_applet_icon_dispose (GObject *object)
{
    GPasteAppletIconPrivate *priv = g_paste_applet_icon_get_instance_private (G_PASTE_APPLET_ICON (object));

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
                         GPasteClient *client)
{
    g_return_val_if_fail (g_type_is_a (type, G_PASTE_TYPE_APPLET_ICON), NULL);
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);

    GPasteAppletIcon *self = g_object_new (type, NULL);
    GPasteAppletIconPrivate *priv = g_paste_applet_icon_get_instance_private (self);

    priv->client = g_object_ref (client);
    priv->show_id = g_signal_connect (client,
                                      "show-history",
                                      G_CALLBACK (g_paste_applet_icon_show_history),
                                      self);

    return self;
}
