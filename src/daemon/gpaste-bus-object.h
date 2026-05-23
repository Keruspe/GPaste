// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste/gpaste-macros.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_BUS_OBJECT (g_paste_bus_object_get_type ())

G_PASTE_DERIVABLE_TYPE (BusObject, bus_object, BUS_OBJECT, GObject)

struct _GPasteBusObjectClass
{
    GObjectClass parent_class;

    /*< pure virtual >*/
    gboolean (*register_on_connection) (GPasteBusObject *self,
                                        GDBusConnection *connection,
                                        GError         **error);
};

gboolean g_paste_bus_object_register_on_connection (GPasteBusObject *self,
                                                    GDBusConnection *connection,
                                                    GError         **error);

G_END_DECLS
