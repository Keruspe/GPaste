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

#include <gpaste-bus.h>
#include <gpaste-gdbus-defines.h>

#include <string.h>

struct _GPasteBus
{
    GObject parent_instance;
};

typedef struct
{
    GDBusConnection         *connection;
    guint                    id_on_bus;
} GPasteBusPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteBus, g_paste_bus, G_TYPE_OBJECT)

static void
g_paste_bus_on_bus_acquired (GDBusConnection *connection,
                             const char      *name G_GNUC_UNUSED,
                             gpointer         user_data)
{
    GPasteBusPrivate *priv = g_paste_bus_get_instance_private (user_data);

    priv->connection = g_object_ref (connection);
}

/**
 * g_paste_bus_own_bus_name:
 * @self: the #GPasteBus
 *
 * Own the bus name
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_bus_own_bus_name (GPasteBus *self)
{
    g_return_if_fail (G_PASTE_IS_BUS (self));

    GPasteBusPrivate *priv = g_paste_bus_get_instance_private (self);

    g_return_if_fail (!priv->id_on_bus);

    priv->id_on_bus = g_bus_own_name (G_BUS_TYPE_SESSION,
                                      G_PASTE_BUS_NAME,
                                      G_BUS_NAME_OWNER_FLAGS_NONE,
                                      g_paste_bus_on_bus_acquired,
                                      NULL, /* on_name_acquired */
                                      NULL, /* on_name_lost */
                                      g_object_ref (self),
                                      g_object_unref);
}

/**
 * g_paste_bus_get_connection:
 * @self: the #GPasteBus
 *
 * returns the #GDBusConnection
 *
 * Returns: (transfer none) (nullable): the connection
 */
G_PASTE_VISIBLE GDBusConnection *
g_paste_bus_get_connection (const GPasteBus *self)
{
    g_return_val_if_fail (G_PASTE_IS_BUS (self), NULL);

    GPasteBusPrivate *priv = g_paste_bus_get_instance_private (self);

    return priv->connection;
}

static void
g_paste_bus_dispose (GObject *object)
{
    GPasteBusPrivate *priv = g_paste_bus_get_instance_private (G_PASTE_BUS (object));

    if (priv->connection)
    {
        g_bus_unown_name (priv->id_on_bus);
        g_clear_object (&priv->connection);
    }

    G_OBJECT_CLASS (g_paste_bus_parent_class)->dispose (object);
}

static void
g_paste_bus_class_init (GPasteBusClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_bus_dispose;
}

static void
g_paste_bus_init (GPasteBus *self)
{
    GPasteBusPrivate *priv = g_paste_bus_get_instance_private (self);

    priv->id_on_bus = 0;
}

/**
 * g_paste_bus_new:
 *
 * Create a new instance of #GPasteBus
 *
 * Returns: a newly allocated #GPasteBus
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteBus *
g_paste_bus_new (void)
{
    return G_PASTE_BUS (g_object_new (G_PASTE_TYPE_BUS, NULL));
}
