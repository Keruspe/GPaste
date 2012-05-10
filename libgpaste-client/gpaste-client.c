/*
 *      This file is part of GPaste.
 *
 *      Copyright 2012 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-client-private.h"

#include <gio/gio.h>

#define G_PASTE_CLIENT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_CLIENT, GPasteClientPrivate))

G_DEFINE_TYPE (GPasteClient, g_paste_client, G_TYPE_OBJECT)

struct _GPasteClientPrivate
{
    GDBusProxy *proxy;
};

static void
g_paste_client_dispose (GObject *object)
{
    g_object_unref (G_PASTE_CLIENT (object)->priv->proxy);

    G_OBJECT_CLASS (g_paste_client_parent_class)->dispose (object);
}

static void
g_paste_client_finalize (GObject *object)
{
    G_OBJECT_CLASS (g_paste_client_parent_class)->finalize (object);
}

static void
g_paste_client_class_init (GPasteClientClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPasteClientPrivate));

    G_OBJECT_CLASS (klass)->dispose = g_paste_client_dispose;
    G_OBJECT_CLASS (klass)->finalize = g_paste_client_finalize;
}

static void
g_paste_client_init (GPasteClient *self)
{
    GPasteClientPrivate *priv = self->priv = G_PASTE_CLIENT_GET_PRIVATE (self);

    priv->proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                 G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                                 NULL, /* interface_info */
                                                 "org.gnome.GPaste",
                                                 "/org/gnome/GPaste",
                                                 "org.gnome.GPaste",
                                                 NULL, /* cancellable */
                                                 NULL); /* error */
}

/**
 * g_paste_client_new:
 *
 * Create a new instance of #GPasteClient
 *
 * Returns: a newly allocated #GPasteClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClient *
g_paste_client_new (void)
{
    return g_object_new (G_PASTE_TYPE_CLIENT, NULL);
}
