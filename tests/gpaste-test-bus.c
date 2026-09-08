// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>

/* A second connection of its own to the private bus, so that a test can own the
 * well-known name from one and hand it to another the way a restart does. */
GDBusConnection *
g_paste_test_bus_new_server (GTestDBus                  *bus,
                             const gchar                *path,
                             GDBusInterfaceInfo         *interface,
                             const GDBusInterfaceVTable *vtable,
                             gpointer                    user_data,
                             guint                      *registration)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusConnection) connection = g_dbus_connection_new_for_address_sync (
        g_test_dbus_get_bus_address (bus),
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL, NULL, &error);
    g_assert_no_error (error);
    *registration = g_dbus_connection_register_object (connection, path, interface, vtable,
                                                       user_data, NULL, &error);
    g_assert_no_error (error);

    return g_steal_pointer (&connection);
}

guint32
g_paste_test_bus_name_call (GDBusConnection *connection,
                            const gchar     *method,
                            GVariant        *parameters)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GVariant) reply = g_dbus_connection_call_sync (connection,
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", method,
        parameters, G_VARIANT_TYPE ("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_assert_no_error (error);
    guint32 result;
    g_variant_get (reply, "(u)", &result);

    return result;
}

static void
on_barrier (GObject      *source,
            GAsyncResult *result,
            gpointer      user_data)
{
    gboolean *done = user_data;
    g_autoptr (GError) error = NULL;
    g_autoptr (GVariant) reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
    g_assert_no_error (error);
    g_assert_nonnull (reply);
    *done = TRUE;
}

/* A reply behind the calls on the client's connection makes no-op assertions
 * deterministic: a call that was sent has reached the fake service by now.
 * The later rounds also drain the calls a reply's own callback issues, and the
 * ones a handoff leaves crossing between the two servers.
 * Addressed to @server's unique name rather than the well-known one, so it is
 * the server that is up that answers -- including while nobody owns the name. */
void
g_paste_test_bus_barrier (GDBusConnection *connection,
                          GDBusConnection *server,
                          const gchar     *path,
                          const gchar     *interface)
{
    for (guint i = 0; i < 3; i++)
    {
        gboolean done = FALSE;

        g_dbus_connection_call (connection, g_dbus_connection_get_unique_name (server), path,
                                interface, "Barrier", NULL, NULL,
                                G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_barrier, &done);
        while (!done)
            g_main_context_iteration (NULL, TRUE);
    }
}
