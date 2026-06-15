// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-bus-object.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_BUS (g_paste_bus_get_type ())

G_PASTE_FINAL_TYPE (Bus, bus, BUS, GObject)

/* Hand the bus an object to expose. It is registered immediately if the name is
 * already owned, otherwise when it is acquired; the bus keeps it alive and emits
 * "name-lost" if registration fails. */
void g_paste_bus_add_object (GPasteBus       *self,
                             GPasteBusObject *object);

void g_paste_bus_own_name (GPasteBus *self);

void g_paste_bus_unown_name (GPasteBus *self);

GPasteBus *g_paste_bus_new (void);

G_END_DECLS
