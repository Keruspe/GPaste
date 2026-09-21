// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

/* How long a wait below goes on before it fails: every one of them waits on
 * something a second or so away at most, and ten leave room for a loaded
 * machine without hanging on a regression. */
#define G_PASTE_TEST_BUS_WAIT_SECONDS 10

typedef gboolean (*GPasteTestBusDone) (gconstpointer data,
                                       gconstpointer arg);

GDBusConnection *g_paste_test_bus_connect        (const gchar                *address);
GDBusConnection *g_paste_test_bus_new_server     (GTestDBus                  *bus,
                                                  const gchar                *path,
                                                  GDBusInterfaceInfo         *interface,
                                                  const GDBusInterfaceVTable *vtable,
                                                  gpointer                    user_data,
                                                  guint                      *registration);
guint32          g_paste_test_bus_name_call      (GDBusConnection            *connection,
                                                  const gchar                *method,
                                                  GVariant                   *parameters);
void             g_paste_test_bus_barrier        (GDBusConnection            *connection,
                                                  GDBusConnection            *server,
                                                  const gchar                *path,
                                                  const gchar                *interface);
void             g_paste_test_bus_wait_until     (GPasteTestBusDone           done,
                                                  gconstpointer               data,
                                                  gconstpointer               arg);
gboolean         g_paste_test_bus_is_set         (gconstpointer               pointer,
                                                  gconstpointer               arg);
void             g_paste_test_bus_wait_for_enum  (gpointer                    object,
                                                  const gchar                *property,
                                                  gint                        value);
void             g_paste_test_bus_wait_for_count (const guint                *count,
                                                  guint                       at_least);
void             g_paste_test_bus_count_emission (guint                      *count);
void             g_paste_test_bus_wait_for_owner (GDBusProxy                 *proxy,
                                                  GDBusConnection            *server);
void             g_paste_test_bus_pump           (guint                       ms);
void             g_paste_test_bus_round_trip     (GDBusConnection            *connection,
                                                  GDBusConnection            *server);

G_END_DECLS
