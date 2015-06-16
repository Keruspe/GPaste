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

#include "gpaste-bus-object.h"

G_DEFINE_ABSTRACT_TYPE (GPasteBusObject, g_paste_bus_object, G_TYPE_OBJECT)

/**
 * g_paste_bus_object_register_on_connection:
 * @self: a #GPasteBusObject
 * @connection: a #GDBusConnection
 * @error: a #GError
 *
 * Register the #GPasteBusObject on the connection
 *
 * Returns: Whether the action succeeded or not
 */
G_PASTE_VISIBLE gboolean
g_paste_bus_object_register_on_connection (GPasteBusObject *self,
                                           GDBusConnection *connection,
                                           GError         **error)
{
    g_return_val_if_fail (G_PASTE_IS_BUS_OBJECT (self), FALSE);
    g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), FALSE);
    g_return_val_if_fail (!error || !(*error), FALSE);

    return G_PASTE_BUS_OBJECT_GET_CLASS (self)->register_on_connection (self, connection, error);
}

static void
g_paste_bus_object_class_init (GPasteBusObjectClass *klass)
{
    klass->register_on_connection = NULL;
}

static void
g_paste_bus_object_init (GPasteBusObject *self G_GNUC_UNUSED)
{
}
