// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include "gpaste-bus-object.h"

G_PASTE_DEFINE_ABSTRACT_TYPE (BusObject, bus_object, G_TYPE_OBJECT)

/**
 * g_paste_bus_object_register_on_connection:
 * @self: a #GPasteBusObject
 * @connection: a #GDBusConnection
 * @error: return location for a #GError, or %NULL
 *
 * Register the #GPasteBusObject on the connection
 *
 * Failures come from GDBus registering the object, so they land in %G_IO_ERROR.
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

/**
 * g_paste_bus_object_unregister_on_connection:
 * @self: a #GPasteBusObject
 *
 * Unexport the #GPasteBusObject from the connection it was registered on.
 *
 * g_dbus_connection_register_object() keeps a reference on @self as its
 * user_data, so an object that is never unregistered can never be finalized —
 * and its object path stays exported on a connection that outlives it (the
 * shared session bus inside gnome-shell). Unregistering here breaks that cycle,
 * so a later registration at the same path succeeds instead of failing with
 * %G_IO_ERROR_EXISTS.
 */
G_PASTE_VISIBLE void
g_paste_bus_object_unregister_on_connection (GPasteBusObject *self)
{
    g_return_if_fail (G_PASTE_IS_BUS_OBJECT (self));

    if (G_PASTE_BUS_OBJECT_GET_CLASS (self)->unregister_on_connection)
        G_PASTE_BUS_OBJECT_GET_CLASS (self)->unregister_on_connection (self);
}

static void
g_paste_bus_object_class_init (GPasteBusObjectClass *klass)
{
    klass->register_on_connection = NULL;
    klass->unregister_on_connection = NULL;
}

static void
g_paste_bus_object_init (GPasteBusObject *self G_GNUC_UNUSED)
{
}
