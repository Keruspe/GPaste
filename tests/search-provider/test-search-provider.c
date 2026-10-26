// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* The overview's search provider, the daemon's own sources compiled in,
 * against a fake daemon on a private bus. */

#include <gpaste/gpaste-gdbus-defines.h>
#include <gpaste-search-provider.h>
#include <gpaste-test-bus.h>

#define ITEM_UUID "00000000-0000-4000-8000-000000000000"

static void
on_daemon_call (GDBusConnection       *connection G_GNUC_UNUSED,
                const gchar           *sender G_GNUC_UNUSED,
                const gchar           *path G_GNUC_UNUSED,
                const gchar           *interface,
                const gchar           *method,
                GVariant              *parameters G_GNUC_UNUSED,
                GDBusMethodInvocation *invocation,
                gpointer               user_data G_GNUC_UNUSED)
{
    if (g_str_equal (interface, "org.freedesktop.DBus.Properties"))
        g_dbus_method_invocation_return_value (invocation, g_variant_new_parsed ("(@a{sv} {},)"));
    else if (g_str_equal (method, G_PASTE_DAEMON_GET_ELEMENTS))
        g_dbus_method_invocation_return_value (invocation, g_variant_new_parsed ("([(%s, 'a password, as its value')],)", ITEM_UUID));
    else
        g_dbus_method_invocation_return_dbus_error (invocation, "org.freedesktop.DBus.Error.UnknownMethod", method);
}

static const GDBusInterfaceVTable daemon_vtable = { on_daemon_call, NULL, NULL, { 0 } };

static void
on_metas (GObject      *source,
          GAsyncResult *result,
          gpointer      user_data)
{
    g_autoptr (GError) error = NULL;

    *((GVariant **) user_data) = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
    g_assert_no_error (error);
}

/* The metas of what the provider found, once its client is up: it answers
 * none before. */
static GVariant *
result_metas (GDBusConnection *connection)
{
    for (guint i = 0; i < 500; ++i)
    {
        GVariant *answer = NULL;
        const gchar *uuids[] = { ITEM_UUID, NULL };

        g_dbus_connection_call (connection, g_dbus_connection_get_unique_name (connection),
                                G_PASTE_SEARCH_PROVIDER_OBJECT_PATH, G_PASTE_SEARCH_PROVIDER_INTERFACE_NAME,
                                G_PASTE_SEARCH_PROVIDER_GET_RESULT_METAS, g_variant_new ("(^as)", uuids),
                                G_VARIANT_TYPE ("(aa{sv})"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_metas, &answer);
        while (!answer)
            g_main_context_iteration (NULL, TRUE);

        g_autoptr (GVariant) metas = g_variant_get_child_value (answer, 0);

        if (g_variant_n_children (metas))
        {
            g_variant_unref (answer);
            return g_steal_pointer (&metas);
        }

        g_variant_unref (answer);
        g_usleep (10 * 1000);
        while (g_main_context_iteration (NULL, FALSE))
            ;
    }

    g_assert_not_reached ();
}

/* A result's metas carry no clipboardText: see on_elements_ready () in
 * gpaste-search-provider.c. */
static void
test_no_clipboard_text (void)
{
    g_autoptr (GTestDBus) bus = g_test_dbus_new (G_TEST_DBUS_NONE);
    g_autoptr (GError) error = NULL;

    g_test_dbus_up (bus);

    g_autoptr (GDBusNodeInfo) info = g_dbus_node_info_new_for_xml (G_PASTE_DAEMON_INTERFACE, &error);

    g_assert_no_error (error);

    guint registration = 0;
    g_autoptr (GDBusConnection) daemon = g_paste_test_bus_new_server (bus, G_PASTE_DAEMON_OBJECT_PATH, info->interfaces[0],
                                                                      &daemon_vtable, NULL, &registration);

    g_assert_cmpuint (g_paste_test_bus_name_call (daemon, "RequestName", g_variant_new ("(su)", G_PASTE_BUS_NAME, 0)), ==, 1);

    g_autoptr (GDBusConnection) connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

    g_assert_no_error (error);

    g_autoptr (GPasteBusObject) provider = g_paste_search_provider_new ();

    g_assert_true (g_paste_bus_object_register_on_connection (provider, connection, &error));
    g_assert_no_error (error);

    g_autoptr (GVariant) metas = result_metas (connection);
    g_autoptr (GVariant) meta = g_variant_get_child_value (metas, 0);
    g_autoptr (GVariantDict) dict = g_variant_dict_new (meta);

    g_assert_true (g_variant_dict_contains (dict, "id"));
    g_assert_true (g_variant_dict_contains (dict, "name"));
    g_assert_false (g_variant_dict_contains (dict, "clipboardText"));

    /* Its registration holds it, and dispose () is what lets that go, along
     * with the client holding the bus g_test_dbus_down () waits on. */
    g_object_run_dispose (G_OBJECT (provider));
    g_clear_object (&provider);
    g_dbus_connection_unregister_object (daemon, registration);
    g_clear_object (&connection);
    g_clear_object (&daemon);
    g_test_dbus_down (bus);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/search-provider/no-clipboard-text", test_no_clipboard_text);

    return g_test_run ();
}
