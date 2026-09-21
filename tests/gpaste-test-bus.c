// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>

/* A second connection of its own to the private bus, so that a test can own the
 * well-known name from one and hand it to another the way a restart does --
 * or, on the bus gpaste-test-env set up, stand in for a daemon coming and
 * going under a client that follows it. */
GDBusConnection *
g_paste_test_bus_connect (const gchar *address)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusConnection) connection = g_dbus_connection_new_for_address_sync (
        address,
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL, NULL, &error);
    g_assert_no_error (error);

    return g_steal_pointer (&connection);
}

GDBusConnection *
g_paste_test_bus_new_server (GTestDBus                  *bus,
                             const gchar                *path,
                             GDBusInterfaceInfo         *interface,
                             const GDBusInterfaceVTable *vtable,
                             gpointer                    user_data,
                             guint                      *registration)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusConnection) connection = g_paste_test_bus_connect (g_test_dbus_get_bus_address (bus));

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

static gboolean
on_wait_timeout (gpointer user_data)
{
    *(gboolean *) user_data = TRUE;

    return G_SOURCE_REMOVE;
}

/* The main context iterated until @done says so, or until the deadline: the
 * caller asserts what it waited for, which says what timed out. */
void
g_paste_test_bus_wait_until (GPasteTestBusDone done,
                             gconstpointer     data,
                             gconstpointer     arg)
{
    gboolean timed_out = FALSE;
    guint source = g_timeout_add_seconds (G_PASTE_TEST_BUS_WAIT_SECONDS, on_wait_timeout, &timed_out);

    g_source_set_name_by_id (source, "[GPaste] test wait deadline");

    while (!done (data, arg) && !timed_out)
        g_main_context_iteration (NULL, TRUE);

    if (!timed_out)
        g_source_remove (source);
}

static gint
read_enum (gconstpointer object,
           const gchar  *property)
{
    gint value;

    g_object_get ((gpointer) object, property, &value, NULL);

    return value;
}

typedef struct
{
    const gchar *property;
    gint         value;
} EnumWait;

static gboolean
enum_reached (gconstpointer object,
              gconstpointer arg)
{
    const EnumWait *wait = arg;

    return read_enum (object, wait->property) == wait->value;
}

/* For a property that moves on its own, such as a client's daemon-presence
 * across its wait for a daemon. */
void
g_paste_test_bus_wait_for_enum (gpointer     object,
                                const gchar *property,
                                gint         value)
{
    EnumWait wait = { property, value };

    g_paste_test_bus_wait_until (enum_reached, object, &wait);
    g_assert_cmpint (read_enum (object, property), ==, value);
}

static gboolean
count_reached (gconstpointer count,
               gconstpointer at_least)
{
    return *(const guint *) count >= GPOINTER_TO_UINT (at_least);
}

/* For a fake service counting the calls it answers. */
void
g_paste_test_bus_wait_for_count (const guint *count,
                                 guint        at_least)
{
    g_paste_test_bus_wait_until (count_reached, count, GUINT_TO_POINTER (at_least));
    g_assert_cmpuint (*count, >=, at_least);
}
