/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include "gpaste-bus-object.h"

G_PASTE_DEFINE_ABSTRACT_TYPE (BusObject, bus_object, G_TYPE_OBJECT)

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
    g_return_val_if_fail (_G_PASTE_IS_BUS_OBJECT (self), FALSE);
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
