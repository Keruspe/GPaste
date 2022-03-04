/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_BUS_OBJECT_H__
#define __G_PASTE_BUS_OBJECT_H__

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

#endif /*__G_PASTE_BUS_OBJECT_H__*/
