/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#pragma once

#include <gpaste/gpaste-macros.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_BUS (g_paste_bus_get_type ())

G_PASTE_FINAL_TYPE (Bus, bus, BUS, GObject)

typedef void (*GPasteBusAcquiredCallback) (GPasteBus *bus,
                                           gpointer   user_data);

void g_paste_bus_own_name (GPasteBus *self);

GDBusConnection *g_paste_bus_get_connection (const GPasteBus *self);

GPasteBus *g_paste_bus_new (GPasteBusAcquiredCallback on_bus_acquired,
                            gpointer                  user_data);

G_END_DECLS
