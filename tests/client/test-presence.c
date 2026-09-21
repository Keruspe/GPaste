// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-client.h>
#include <gpaste-3/gpaste-daemon3.h>
#include <gpaste-3/gpaste-error.h>
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

/* What a real daemon owns the name with: replaceable, and not queued once
 * replaced, as it quits then -- a stand-in queued would get the name back when
 * its successor stops. */
#define REPLACEABLE (G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT | G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE)

/* A daemon stood in for on a connection of its own, owning the name before it
 * exports anything, as both real daemons do. */
static GDBusConnection *
stand_in_connect (GBusNameOwnerFlags flags,
                  guint             *owner)
{
    GDBusConnection *server = g_paste_test_bus_connect (g_getenv ("DBUS_SESSION_BUS_ADDRESS"));

    *owner = g_bus_own_name_on_connection (server, G_PASTE_BUS_NAME, flags, NULL, NULL, NULL, NULL);

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
    g_autoptr (GDBusConnection) server = stand_in_connect (G_BUS_NAME_OWNER_FLAGS_NONE, &owner);

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

/* A stand-in that serves at once, its object exported before the bus has
 * handed it the name: its name, its object and its history --
 * all a client following a daemon reads, which is why it is not the UI
 * suite's FakeDaemon, made to answer what a window asks. */
typedef struct
{
    GDBusConnection *server;
    guint            owner;
    GPasteDaemon3   *skeleton;
} StandIn;

static void
stand_in_serve (StandIn           *stand_in,
                GBusNameOwnerFlags flags)
{
    g_autoptr (GError) error = NULL;

    stand_in->server = stand_in_connect (flags, &stand_in->owner);
    stand_in->skeleton = g_paste_daemon3_skeleton_new ();
    g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (stand_in->skeleton), stand_in->server,
                                      G_PASTE_DAEMON_OBJECT_PATH, &error);
    g_assert_no_error (error);
    g_paste_daemon3_set_history (stand_in->skeleton, "history");
}

/* Gone as a real daemon goes, its connection closing first. */
static void
stand_in_stop (StandIn *stand_in)
{
    g_dbus_connection_close_sync (stand_in->server, NULL, NULL);
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (stand_in->skeleton));
    g_clear_object (&stand_in->skeleton);
    g_clear_handle_id (&stand_in->owner, g_bus_unown_name);
    g_clear_object (&stand_in->server);
}

static gboolean
is_true (gconstpointer flag,
         gconstpointer arg G_GNUC_UNUSED)
{
    return *(const gboolean *) flag;
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

/* Each presence a client announces, and the owner its proxy addressed when it
 * announced it: what a consumer reading the daemon on that edge asks. */
typedef struct
{
    GPasteClient *client;
    GArray       *presences;
    GPtrArray    *owners;
} PresenceLog;

static void
on_presence_logged (GPasteClient *client,
                    GParamSpec   *pspec G_GNUC_UNUSED,
                    gpointer      user_data)
{
    PresenceLog *log = user_data;
    GPasteDaemonPresence presence = g_paste_client_get_daemon_presence (client);

    g_array_append_val (log->presences, presence);
    g_ptr_array_add (log->owners, g_dbus_proxy_get_name_owner (G_DBUS_PROXY (client)));
}

static void
presence_log_start (PresenceLog  *log,
                    GPasteClient *client)
{
    log->client = client;
    log->presences = g_array_new (FALSE, FALSE, sizeof (GPasteDaemonPresence));
    log->owners = g_ptr_array_new_with_free_func (g_free);
    g_signal_connect (client, "notify::daemon-presence", G_CALLBACK (on_presence_logged), log);
}

static void
presence_log_stop (PresenceLog *log)
{
    g_signal_handlers_disconnect_by_data (log->client, log);
    g_clear_pointer (&log->presences, g_array_unref);
    g_clear_pointer (&log->owners, g_ptr_array_unref);
}

static GPasteDaemonPresence
presence_log_at (PresenceLog *log,
                 guint        index)
{
    return g_array_index (log->presences, GPasteDaemonPresence, index);
}

/* A daemon taking the name over straight from another is a presence edge like
 * any other: not ready from the moment the bus hands the name over, however
 * soon the successor serves, and ready again only once the proxy addresses it
 * -- so what a consumer asks on that edge reaches the new daemon, not the one
 * standing down (on_name_owner_changed ()). */
static void
test_handoff (void)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    StandIn first = { 0 };
    StandIn second = { 0 };
    PresenceLog log = { 0 };

    g_assert_no_error (error);

    stand_in_serve (&first, REPLACEABLE);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_test_bus_wait_for_owner (G_DBUS_PROXY (client), first.server);

    presence_log_start (&log, client);
    stand_in_serve (&second, G_BUS_NAME_OWNER_FLAGS_REPLACE);
    g_paste_test_bus_wait_for_owner (G_DBUS_PROXY (client), second.server);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);

    g_assert_cmpuint (log.presences->len, ==, 2);
    g_assert_cmpint (presence_log_at (&log, 0), ==, G_PASTE_DAEMON_PRESENCE_STARTING);
    g_assert_cmpint (presence_log_at (&log, 1), ==, G_PASTE_DAEMON_PRESENCE_READY);
    g_assert_cmpstr (g_ptr_array_index (log.owners, 1), ==, g_dbus_connection_get_unique_name (second.server));
    presence_log_stop (&log);

    stand_in_stop (&second);
    stand_in_stop (&first);
}

/* The same for a client built while a daemon already serves -- a window
 * opened, the Shell menu enabled, the usual case -- whose init found the
 * owner rather than being told of it; and for a successor that, as both real
 * daemons do, owns the name before it serves anything, the presence staying
 * starting until it does. */
static void
test_handoff_after_init (void)
{
    StandIn first = { 0 };
    StandIn second = { 0 };
    g_autoptr (GObject) client = NULL;

    stand_in_serve (&first, REPLACEABLE);
    /* Asynchronously, for the reason test_initables () gives. */
    g_paste_client_new (on_initable_ready, &client);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &client, NULL);
    g_assert_nonnull (client);
    wait_for_presence (G_PASTE_CLIENT (client), G_PASTE_DAEMON_PRESENCE_READY);

    guint silent_owner;
    g_autoptr (GDBusConnection) silent = stand_in_connect (G_BUS_NAME_OWNER_FLAGS_REPLACE | REPLACEABLE, &silent_owner);

    wait_for_presence (G_PASTE_CLIENT (client), G_PASTE_DAEMON_PRESENCE_STARTING);
    g_paste_test_bus_wait_for_owner (G_DBUS_PROXY (client), silent);
    g_assert_cmpint (g_paste_client_get_daemon_presence (G_PASTE_CLIENT (client)), ==, G_PASTE_DAEMON_PRESENCE_STARTING);

    /* That one replaced in turn by one that serves: ready, once. */
    PresenceLog log = { 0 };

    presence_log_start (&log, G_PASTE_CLIENT (client));
    stand_in_serve (&second, G_BUS_NAME_OWNER_FLAGS_REPLACE);
    g_paste_test_bus_wait_for_owner (G_DBUS_PROXY (client), second.server);
    wait_for_presence (G_PASTE_CLIENT (client), G_PASTE_DAEMON_PRESENCE_READY);
    g_assert_cmpuint (log.presences->len, ==, 1);
    g_assert_cmpstr (g_ptr_array_index (log.owners, 0), ==, g_dbus_connection_get_unique_name (second.server));
    presence_log_stop (&log);

    stand_in_stop (&second);
    g_dbus_connection_close_sync (silent, NULL, NULL);
    g_bus_unown_name (silent_owner);
    stand_in_stop (&first);
}

/* A client built through the initables for a name of its own follows that
 * name's handoffs, and only those: one of GPaste's leaves it as it was. */
static void
test_handoff_other_name (void)
{
    static const gchar *other_name = "org.gnome.GPaste.Test.Other";
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteDaemon3) skeleton = g_paste_daemon3_skeleton_new ();
    g_autoptr (GDBusConnection) other = g_paste_test_bus_connect (g_getenv ("DBUS_SESSION_BUS_ADDRESS"));

    g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton), other, G_PASTE_DAEMON_OBJECT_PATH, &error);
    g_assert_no_error (error);
    g_paste_daemon3_set_history (skeleton, "history");

    guint other_owner = g_bus_own_name_on_connection (other, other_name, G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL, NULL);
    g_autoptr (GObject) client = NULL;

    /* Asynchronously, for the reason test_initables () gives. */
    g_async_initable_new_async (G_PASTE_TYPE_CLIENT, G_PRIORITY_DEFAULT, NULL, on_initable_ready, &client,
                                "g-bus-type",       G_BUS_TYPE_SESSION,
                                "g-name",           other_name,
                                "g-object-path",    G_PASTE_DAEMON_OBJECT_PATH,
                                "g-interface-name", G_PASTE_DAEMON_INTERFACE_NAME,
                                NULL);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &client, NULL);
    g_assert_nonnull (client);
    wait_for_presence (G_PASTE_CLIENT (client), G_PASTE_DAEMON_PRESENCE_READY);

    /* GPaste's own name changing hands, watched through a client of its own. */
    g_autoptr (GPasteClient) watcher = g_paste_client_new_sync (&error);
    StandIn first = { 0 };
    StandIn second = { 0 };

    g_assert_no_error (error);
    stand_in_serve (&first, REPLACEABLE);
    g_paste_test_bus_wait_for_owner (G_DBUS_PROXY (watcher), first.server);
    stand_in_serve (&second, G_BUS_NAME_OWNER_FLAGS_REPLACE);
    g_paste_test_bus_wait_for_owner (G_DBUS_PROXY (watcher), second.server);
    g_paste_test_bus_round_trip (g_dbus_proxy_get_connection (G_DBUS_PROXY (client)), other);
    g_assert_cmpint (g_paste_client_get_daemon_presence (G_PASTE_CLIENT (client)), ==, G_PASTE_DAEMON_PRESENCE_READY);

    stand_in_stop (&second);
    stand_in_stop (&first);
    g_dbus_connection_close_sync (other, NULL, NULL);
    g_bus_unown_name (other_owner);
}

/* A call failing because its daemon left the bus, rather than refusing it. An
 * unknown method is not one: a handoff moves the presence before the daemon
 * standing down can answer one (test_handoff ()), and otherwise it is a daemon
 * older than the method. GDBus, which both daemons use, answers an unexported
 * object or interface with UnknownMethod too. */
static void
test_daemon_gone_error (void)
{
    g_autoptr (GError) no_reply = g_error_new_literal (G_DBUS_ERROR, G_DBUS_ERROR_NO_REPLY, "gone");
    g_autoptr (GError) unknown = g_error_new_literal (G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN, "gone");
    g_autoptr (GError) no_owner = g_error_new_literal (G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER, "gone");
    g_autoptr (GError) refused = g_error_new_literal (G_PASTE_ERROR, G_PASTE_ERROR_NOT_FOUND, "no such item");
    g_autoptr (GError) timed_out = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "slow");
    g_autoptr (GError) unknown_method = g_error_new_literal (G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "older");

    g_assert_true (g_paste_client_is_daemon_gone_error (no_reply));
    g_assert_true (g_paste_client_is_daemon_gone_error (unknown));
    g_assert_true (g_paste_client_is_daemon_gone_error (no_owner));
    g_assert_false (g_paste_client_is_daemon_gone_error (unknown_method));
    g_assert_false (g_paste_client_is_daemon_gone_error (refused));
    g_assert_false (g_paste_client_is_daemon_gone_error (timed_out));
    g_assert_false (g_paste_client_is_daemon_gone_error (NULL));
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

/* A client built the way a binding builds one, through the initables rather
 * than g_paste_client_new*(), starts out knowing the daemon is there. */
static void
test_initables (void)
{
    g_autoptr (GError) error = NULL;
    guint owner;
    g_autoptr (GDBusConnection) server = stand_in_connect (G_BUS_NAME_OWNER_FLAGS_NONE, &owner);
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

    g_signal_connect_swapped (watched, "notify::daemon-presence", G_CALLBACK (g_paste_test_bus_count_emission), &notified);
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
    g_autoptr (GDBusConnection) server = stand_in_connect (G_BUS_NAME_OWNER_FLAGS_NONE, &owner);
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
    g_autoptr (GDBusConnection) server = stand_in_connect (G_BUS_NAME_OWNER_FLAGS_NONE, &owner);
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

    g_signal_connect_swapped (watched, "notify::daemon-presence", G_CALLBACK (g_paste_test_bus_count_emission), &notified);
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
    g_test_add_func ("/client/presence/handoff", test_handoff);
    g_test_add_func ("/client/presence/handoff-after-init", test_handoff_after_init);
    g_test_add_func ("/client/presence/handoff-other-name", test_handoff_other_name);
    g_test_add_func ("/client/presence/daemon-gone-error", test_daemon_gone_error);
    return g_paste_test_env_run ();
}
