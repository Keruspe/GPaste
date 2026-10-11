// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* The wait a followed client gives an owner that never serves, two minutes cut
 * to two seconds by compiling the client in: once it runs out the owner reads
 * as absent, and a new owner gets a wait of its own, starting by the time a
 * handler of anything it announces reads the presence. */

#include <gpaste-test-bus.h>
#include <gpaste-test-env.h>

#define G_PASTE_CLIENT_GIVE_UP_SECONDS 2
#include <gpaste-3/gpaste-client.c>

static void
wait_for_presence (GPasteClient        *client,
                   GPasteDaemonPresence presence)
{
    g_paste_test_bus_wait_for_enum (client, "daemon-presence", presence);
}

static void
record_presence (GObject    *object,
                 GParamSpec *pspec G_GNUC_UNUSED,
                 gpointer    user_data)
{
    *(GPasteDaemonPresence *) user_data = g_paste_client_get_daemon_presence (G_PASTE_CLIENT (object));
}

static void
record_presence_tracking (GPasteClient *client,
                          gboolean      tracking G_GNUC_UNUSED,
                          gpointer      user_data)
{
    *(GPasteDaemonPresence *) user_data = g_paste_client_get_daemon_presence (client);
}

static void
test_new_owner (void)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    g_autoptr (GDBusConnection) server = g_paste_test_bus_connect (g_getenv ("DBUS_SESSION_BUS_ADDRESS"));
    GPasteDaemonPresence at_active = G_PASTE_DAEMON_PRESENCE_READY;
    GPasteDaemonPresence at_tracking = G_PASTE_DAEMON_PRESENCE_READY;
    GPasteDaemonPresence at_history = G_PASTE_DAEMON_PRESENCE_READY;

    g_assert_no_error (error);
    g_paste_client_follow_daemon (client, FALSE);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_ABSENT);

    /* Owned, never served: starting until the wait runs out. */
    guint owner = g_bus_own_name_on_connection (server, G_PASTE_BUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL, NULL);

    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_STARTING);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_ABSENT);
    g_paste_test_bus_wait_for_owner (G_DBUS_PROXY (client), server);

    /* Its leaving changes nothing, and the next owner starts over. */
    g_bus_unown_name (owner);
    g_paste_test_bus_wait_for_owner (G_DBUS_PROXY (client), NULL);
    g_assert_cmpint (g_paste_client_get_daemon_presence (client), ==, G_PASTE_DAEMON_PRESENCE_ABSENT);

    g_signal_connect (client, "notify::active", G_CALLBACK (record_presence), &at_active);
    g_signal_connect (client, "tracking", G_CALLBACK (record_presence_tracking), &at_tracking);
    g_signal_connect (client, "notify::history", G_CALLBACK (record_presence), &at_history);
    owner = g_bus_own_name_on_connection (server, G_PASTE_BUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL, NULL);
    g_paste_test_bus_wait_for_owner (G_DBUS_PROXY (client), server);
    g_assert_cmpint (at_active, ==, G_PASTE_DAEMON_PRESENCE_STARTING);
    g_assert_cmpint (at_tracking, ==, G_PASTE_DAEMON_PRESENCE_STARTING);
    g_assert_cmpint (at_history, ==, G_PASTE_DAEMON_PRESENCE_STARTING);
    g_assert_cmpint (g_paste_client_get_daemon_presence (client), ==, G_PASTE_DAEMON_PRESENCE_STARTING);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_ABSENT);

    g_bus_unown_name (owner);
    g_paste_client_unfollow_daemon (client);
    g_dbus_connection_close_sync (server, NULL, NULL);
}

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/client/give-up/new-owner", test_new_owner);

    return g_paste_test_env_run ();
}
