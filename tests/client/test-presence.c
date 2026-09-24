// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-client.h>
#include <gpaste-3/gpaste-daemon3.h>
#include <gpaste-3/gpaste-gdbus-defines.h>
#include <gpaste-3/gpaste-util.h>
#include <gpaste-test-bus.h>
#include <gpaste-test-env.h>

static void
wait_for_presence (GPasteClient        *client,
                   GPasteDaemonPresence presence)
{
    g_paste_test_bus_wait_for_enum (client, "daemon-presence", presence);
}

/* A daemon stood in for by the generated skeleton on a connection of its own,
 * owning the name before it exports anything, as both real daemons do. */
static GDBusConnection *
stand_in_connect (guint *owner)
{
    GDBusConnection *server = g_paste_test_bus_connect (g_getenv ("DBUS_SESSION_BUS_ADDRESS"));

    *owner = g_bus_own_name_on_connection (server, G_PASTE_BUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
                                           NULL, NULL, NULL, NULL);

    return server;
}

static void
test_follow (void)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);

    g_assert_no_error (error);

    /* Nobody owns the name and the bus has nothing to activate. */
    g_assert_cmpint (g_paste_client_get_daemon_presence (client), ==, G_PASTE_DAEMON_PRESENCE_ABSENT);

    /* Gone for less than the grace second is not yet missing. */
    g_paste_client_follow_daemon (client, FALSE);
    g_assert_cmpint (g_paste_client_get_daemon_presence (client), ==, G_PASTE_DAEMON_PRESENCE_STARTING);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_ABSENT);

    guint owner;
    g_autoptr (GDBusConnection) server = stand_in_connect (&owner);

    /* The name without a history: starting. */
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_STARTING);

    g_autoptr (GPasteDaemon3) skeleton = g_paste_daemon3_skeleton_new ();

    g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton), server, G_PASTE_DAEMON_OBJECT_PATH, &error);
    g_assert_no_error (error);
    g_paste_daemon3_set_history (skeleton, "history");

    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);

    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (skeleton));
    g_bus_unown_name (owner);

    /* Gone again: the grace second passes before it says so. */
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_STARTING);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_ABSENT);

    /* Asking goes to the bus, which has nothing to start. */
    g_paste_client_retry_daemon (client);
    g_assert_cmpint (g_paste_client_get_daemon_presence (client), ==, G_PASTE_DAEMON_PRESENCE_STARTING);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_ABSENT);

    g_paste_client_unfollow_daemon (client);
}

/* Not covered here: a new owner starting the wait afresh, partway through the
 * two minutes or after they ran out (g_paste_client_notify ()), which would
 * hold the suite for those two minutes. */

/* Without following, there is no wait: absent is absent at once. */
static void
test_unfollowed (void)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);

    g_assert_no_error (error);
    g_paste_client_follow_daemon (client, FALSE);
    g_paste_client_unfollow_daemon (client);
    g_assert_cmpint (g_paste_client_get_daemon_presence (client), ==, G_PASTE_DAEMON_PRESENCE_ABSENT);
}

static gboolean
is_true (gconstpointer flag,
         gconstpointer arg G_GNUC_UNUSED)
{
    return *(const gboolean *) flag;
}

static void
count_notify (guint *count)
{
    ++*count;
}

static void
on_init_ready (GObject      *source_object,
               GAsyncResult *res,
               gpointer      user_data)
{
    g_autoptr (GError) error = NULL;

    g_assert_true (g_async_initable_init_finish (G_ASYNC_INITABLE (source_object), res, &error));
    g_assert_no_error (error);
    *(gboolean *) user_data = TRUE;
}

static void
on_initable_ready (GObject      *source_object,
                   GAsyncResult *res,
                   gpointer      user_data)
{
    GObject **client = user_data;
    g_autoptr (GError) error = NULL;

    *client = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, &error);
    g_assert_no_error (error);
}

/* A client built the way a binding builds one, through the initables rather
 * than g_paste_client_new*(), starts out knowing the daemon is there. */
static void
test_initables (void)
{
    g_autoptr (GError) error = NULL;
    guint owner;
    g_autoptr (GDBusConnection) server = stand_in_connect (&owner);
    g_autoptr (GPasteDaemon3) skeleton = g_paste_daemon3_skeleton_new ();

    g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton), server, G_PASTE_DAEMON_OBJECT_PATH, &error);
    g_assert_no_error (error);
    g_paste_daemon3_set_history (skeleton, "history");

    /* Asynchronously first: the stand-in answers the proxy's GetAll on this
     * thread's main context, which a synchronous initialisation would block
     * until it timed out. */
    g_autoptr (GObject) async_client = NULL;

    g_async_initable_new_async (G_PASTE_TYPE_CLIENT, G_PRIORITY_DEFAULT, NULL, on_initable_ready, &async_client,
                                "g-bus-type",       G_BUS_TYPE_SESSION,
                                "g-name",           G_PASTE_BUS_NAME,
                                "g-object-path",    G_PASTE_DAEMON_OBJECT_PATH,
                                "g-interface-name", G_PASTE_DAEMON_INTERFACE_NAME,
                                NULL);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &async_client, NULL);
    g_assert_nonnull (async_client);
    g_assert_cmpint (g_paste_client_get_daemon_presence (G_PASTE_CLIENT (async_client)), ==, G_PASTE_DAEMON_PRESENCE_READY);

    /* Built in two steps, with a handler watching before the init: it hears of
     * the daemon being there, once. */
    g_autoptr (GObject) watched = g_object_new (G_PASTE_TYPE_CLIENT,
                                                "g-bus-type",       G_BUS_TYPE_SESSION,
                                                "g-name",           G_PASTE_BUS_NAME,
                                                "g-object-path",    G_PASTE_DAEMON_OBJECT_PATH,
                                                "g-interface-name", G_PASTE_DAEMON_INTERFACE_NAME,
                                                NULL);
    guint notified = 0;
    gboolean initialised = FALSE;

    g_signal_connect_swapped (watched, "notify::daemon-presence", G_CALLBACK (count_notify), &notified);
    g_async_initable_init_async (G_ASYNC_INITABLE (watched), G_PRIORITY_DEFAULT, NULL, on_init_ready, &initialised);
    g_paste_test_bus_wait_until (is_true, &initialised, NULL);
    g_assert_true (initialised);
    g_assert_cmpint (g_paste_client_get_daemon_presence (G_PASTE_CLIENT (watched)), ==, G_PASTE_DAEMON_PRESENCE_READY);
    g_assert_cmpuint (notified, ==, 1);

    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (skeleton));
    g_bus_unown_name (owner);
    wait_for_presence (G_PASTE_CLIENT (async_client), G_PASTE_DAEMON_PRESENCE_ABSENT);

    /* With the daemon gone, the synchronous one has nothing to wait on. */
    g_autoptr (GPasteClient) sync_client = G_PASTE_CLIENT (g_initable_new (G_PASTE_TYPE_CLIENT, NULL, &error,
                                                                           "g-bus-type",       G_BUS_TYPE_SESSION,
                                                                           "g-name",           G_PASTE_BUS_NAME,
                                                                           "g-object-path",    G_PASTE_DAEMON_OBJECT_PATH,
                                                                           "g-interface-name", G_PASTE_DAEMON_INTERFACE_NAME,
                                                                           NULL));

    g_assert_no_error (error);
    g_assert_cmpint (g_paste_client_get_daemon_presence (sync_client), ==, G_PASTE_DAEMON_PRESENCE_ABSENT);
}

/* With no get_property () in the vtable, GDBus hands the proxy's GetAll here
 * too: that one is answered with nothing, a daemon still starting having no
 * history to give, and everything else is sat on -- the invocation, which a
 * reply consumes, kept for the test to answer. */
static void
on_method_call (GDBusConnection       *connection     G_GNUC_UNUSED,
                const gchar           *sender         G_GNUC_UNUSED,
                const gchar           *object_path    G_GNUC_UNUSED,
                const gchar           *interface_name,
                const gchar           *method_name    G_GNUC_UNUSED,
                GVariant              *parameters     G_GNUC_UNUSED,
                GDBusMethodInvocation *invocation,
                gpointer               user_data)
{
    GDBusMethodInvocation **held = user_data;

    if (g_paste_str_equal (interface_name, "org.freedesktop.DBus.Properties"))
        g_dbus_method_invocation_return_value (invocation, g_variant_new_parsed ("(@a{sv} {},)"));
    else
        *held = invocation;
}

/* A request for a daemon still out to one that never answers holds nothing
 * that would keep the client alive: dropping the last reference is what runs
 * the dispose () that cancels it. */
static void
test_probe_released (void)
{
    g_autoptr (GError) error = NULL;
    /* Built before the stand-in is there, for the reason test_initables ()
     * gives. */
    GPasteClient *client = g_paste_client_new_sync (&error);

    g_assert_no_error (error);

    /* Registered by hand rather than through the skeleton, which would serve
     * the History property, empty as it is, and read as a daemon ready: this
     * one is still starting, and sits on whatever it is asked. */
    static const GDBusInterfaceVTable vtable = { on_method_call, NULL, NULL, { 0 } };
    GDBusMethodInvocation *held = NULL;
    guint owner;
    g_autoptr (GDBusConnection) server = stand_in_connect (&owner);
    guint registration = g_dbus_connection_register_object (server, G_PASTE_DAEMON_OBJECT_PATH,
                                                            g_paste_daemon3_interface_info (),
                                                            &vtable, &held, NULL, &error);

    g_assert_no_error (error);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_STARTING);

    g_paste_client_follow_daemon (client, TRUE);

    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &held, NULL);
    g_assert_nonnull (held);

    g_object_add_weak_pointer (G_OBJECT (client), (gpointer *) &client);
    g_object_unref (client);
    g_assert_null (client);

    g_dbus_method_invocation_return_value (g_steal_pointer (&held), g_variant_new ("(t)", (guint64) 0));
    g_dbus_connection_unregister_object (server, registration);
    g_bus_unown_name (owner);
}

/* A two-step init against an owner that serves nothing yet: GetAll fills no
 * history, so nothing in GDBusProxy's init moves the presence, and the seed
 * alone tells a handler watching from before the init that a daemon is on its
 * way. (Against a serving daemon, as in test_initables (), the GetAll reply
 * has already said so.) */
static void
test_seed_announced (void)
{
    g_autoptr (GError) error = NULL;
    static const GDBusInterfaceVTable vtable = { on_method_call, NULL, NULL, { 0 } };
    GDBusMethodInvocation *held = NULL;
    guint owner;
    g_autoptr (GDBusConnection) server = stand_in_connect (&owner);
    guint registration = g_dbus_connection_register_object (server, G_PASTE_DAEMON_OBJECT_PATH,
                                                            g_paste_daemon3_interface_info (),
                                                            &vtable, &held, NULL, &error);

    g_assert_no_error (error);

    g_autoptr (GObject) watched = g_object_new (G_PASTE_TYPE_CLIENT,
                                                "g-bus-type",       G_BUS_TYPE_SESSION,
                                                "g-name",           G_PASTE_BUS_NAME,
                                                "g-object-path",    G_PASTE_DAEMON_OBJECT_PATH,
                                                "g-interface-name", G_PASTE_DAEMON_INTERFACE_NAME,
                                                NULL);
    guint notified = 0;
    gboolean initialised = FALSE;

    g_signal_connect_swapped (watched, "notify::daemon-presence", G_CALLBACK (count_notify), &notified);
    g_async_initable_init_async (G_ASYNC_INITABLE (watched), G_PRIORITY_DEFAULT, NULL, on_init_ready, &initialised);
    g_paste_test_bus_wait_until (is_true, &initialised, NULL);
    g_assert_true (initialised);
    g_assert_cmpint (g_paste_client_get_daemon_presence (G_PASTE_CLIENT (watched)), ==, G_PASTE_DAEMON_PRESENCE_STARTING);
    g_assert_cmpuint (notified, ==, 1);

    g_assert_null (held);
    g_dbus_connection_unregister_object (server, registration);
    g_bus_unown_name (owner);
}

int
main (int argc, char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/client/presence/follow", test_follow);
    g_test_add_func ("/client/presence/unfollowed", test_unfollowed);
    g_test_add_func ("/client/presence/initables", test_initables);
    g_test_add_func ("/client/presence/probe-released", test_probe_released);
    g_test_add_func ("/client/presence/seed-announced", test_seed_announced);
    return g_paste_test_env_run ();
}
