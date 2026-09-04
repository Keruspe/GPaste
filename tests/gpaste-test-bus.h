// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

GDBusConnection *g_paste_test_bus_new_server (GTestDBus                  *bus,
                                              const gchar                *path,
                                              GDBusInterfaceInfo         *interface,
                                              const GDBusInterfaceVTable *vtable,
                                              gpointer                    user_data,
                                              guint                      *registration);
guint32          g_paste_test_bus_name_call  (GDBusConnection            *connection,
                                              const gchar                *method,
                                              GVariant                   *parameters);
void             g_paste_test_bus_barrier    (GDBusConnection            *connection,
                                              GDBusConnection            *server,
                                              const gchar                *path,
                                              const gchar                *interface);

G_END_DECLS
