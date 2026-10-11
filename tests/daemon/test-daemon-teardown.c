// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include "gpaste-test-clipboard-provider.h"

#include <gpaste-test-env.h>
#include <gpaste-test-bus.h>
#include <gpaste-daemon/gpaste-global-shortcut-client.h>

/* The private bus has no GlobalShortcuts portal, and a daemon that cannot reach
 * it warns, which g_test makes fatal. Its constructor is never answered here,
 * the reference on the daemon it is handed dropped at once. */
static void
no_portal (GAsyncReadyCallback callback G_GNUC_UNUSED,
           gpointer            user_data)
{
    g_object_unref (user_data);
}

#define g_paste_global_shortcut_client_new no_portal
#include <gpaste-daemon/gpaste-daemon.c>
#undef g_paste_global_shortcut_client_new

typedef struct
{
    GPasteSettings  *settings;
    TestClipboard   *clipboard;
    TestClipboard   *primary;
    GPasteDaemon    *daemon;
    GDBusConnection *connection;
    guint            reexecutions;
    guint            stored_at_reexecution;
} Fixture;

static gboolean
history_loaded (gconstpointer data,
                gconstpointer arg G_GNUC_UNUSED)
{
    return !g_paste_history_is_loading (((GPasteDaemon *) data)->history);
}

/* A daemon exported on the private bus, its history loaded and its screensaver
 * client built: that client's constructor holds a reference on the daemon until
 * it answers, and its answer expects a daemon still up, so a case taking the
 * daemon down waits for it first. */
static void
fixture_set_up (Fixture      *fixture,
                gconstpointer user_data G_GNUC_UNUSED)
{
    fixture->settings = g_paste_settings_new ();
    g_paste_settings_set_history_name (fixture->settings, "daemon-teardown");
    g_paste_settings_set_storage_backend (fixture->settings, G_PASTE_STORAGE_NOOP);
    fixture->clipboard = test_clipboard_new ();
    fixture->primary = test_clipboard_new ();
    fixture->daemon = g_paste_daemon_new (fixture->settings,
                                          G_PASTE_CLIPBOARD_PROVIDER (fixture->clipboard),
                                          G_PASTE_CLIPBOARD_PROVIDER (fixture->primary));
    fixture->connection = g_paste_test_bus_connect (g_getenv ("DBUS_SESSION_BUS_ADDRESS"));

    g_paste_test_bus_wait_until (history_loaded, fixture->daemon, NULL);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &fixture->daemon->screensaver, NULL);
    /* Both waits give up quietly: a case run on a daemon in neither state
     * would test a teardown the real one never gets to. */
    g_assert_false (g_paste_history_is_loading (fixture->daemon->history));
    g_assert_nonnull (fixture->daemon->screensaver);
    g_assert_true (g_paste_bus_object_register_on_connection (G_PASTE_BUS_OBJECT (fixture->daemon), fixture->connection, NULL));
}

static void
fixture_tear_down (Fixture      *fixture,
                   gconstpointer user_data G_GNUC_UNUSED)
{
    g_clear_object (&fixture->connection);
    g_clear_object (&fixture->daemon);
    g_clear_object (&fixture->primary);
    g_clear_object (&fixture->clipboard);
    g_clear_object (&fixture->settings);
}

/* Disposing unregisters, so neither a second dispose nor the unregistration
 * GPasteBus makes when it lets go of the name finds an export to drop. */
static void
test_dispose_twice (Fixture      *fixture,
                    gconstpointer user_data G_GNUC_UNUSED)
{
    g_object_run_dispose (G_OBJECT (fixture->daemon));
    g_assert_null (fixture->daemon->skeleton);
    g_object_run_dispose (G_OBJECT (fixture->daemon));
    g_paste_bus_object_unregister_on_connection (G_PASTE_BUS_OBJECT (fixture->daemon));
}

/* The startup update, looked up through the field the daemon tracks it in. */
static GSource *
startup_update (GPasteDaemon *daemon)
{
    g_assert_cmpuint (daemon->startup_changed_id, !=, 0);

    return g_main_context_find_source_by_id (NULL, daemon->startup_changed_id);
}

/* Unregistering cancels the startup update. Asked of the source rather than
 * through a dispose: disposing clears the daemon's weak references first, and
 * a source left running would then find no daemon and do nothing, which looks
 * the same as a source that is gone. */
static void
test_unregister_cancels_startup (Fixture      *fixture,
                                 gconstpointer user_data G_GNUC_UNUSED)
{
    g_autoptr (GSource) startup = g_source_ref (startup_update (fixture->daemon));

    g_paste_bus_object_unregister_on_connection (G_PASTE_BUS_OBJECT (fixture->daemon));
    g_assert_true (g_source_is_destroyed (startup));
}

/* The startup update holds the daemon weakly: a daemon let go of while the
 * update is pending goes then, not once the update has run. */
static void
test_release_pending_startup (Fixture      *fixture,
                              gconstpointer user_data G_GNUC_UNUSED)
{
    GPasteDaemon *daemon = fixture->daemon;

    g_assert_nonnull (startup_update (daemon));
    g_object_add_weak_pointer (G_OBJECT (daemon), (gpointer *) &daemon);
    g_clear_object (&fixture->daemon);
    g_assert_null (daemon);
}

static void
on_update (GDBusConnection *connection G_GNUC_UNUSED,
           const gchar     *sender G_GNUC_UNUSED,
           const gchar     *path G_GNUC_UNUSED,
           const gchar     *interface G_GNUC_UNUSED,
           const gchar     *signal G_GNUC_UNUSED,
           GVariant        *parameters G_GNUC_UNUSED,
           gpointer         user_data)
{
    guint *updates = user_data;

    ++*updates;
}

/* The startup update reaches the export's clients once it runs, and leaves
 * unregistering nothing of its own to cancel. */
static void
test_startup_update (Fixture      *fixture,
                     gconstpointer user_data G_GNUC_UNUSED)
{
    g_autoptr (GDBusConnection) listener = g_paste_test_bus_connect (g_getenv ("DBUS_SESSION_BUS_ADDRESS"));
    guint updates = 0;
    guint subscription = g_dbus_connection_signal_subscribe (listener, NULL, G_PASTE_DAEMON_INTERFACE_NAME, "Update",
                                                             G_PASTE_DAEMON_OBJECT_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                                             on_update, &updates, NULL);

    /* Looked up before the round trip, which runs whatever is due: a setup that
     * took over a second would otherwise have the update fire there and leave
     * nothing to look up. */
    g_autoptr (GSource) startup = g_source_ref (startup_update (fixture->daemon));

    g_paste_test_bus_round_trip (listener, fixture->connection);
    if (!g_source_is_destroyed (startup))
        g_source_set_ready_time (startup, 0);
    g_paste_test_bus_wait_for_count (&updates, 1);
    g_paste_bus_object_unregister_on_connection (G_PASTE_BUS_OBJECT (fixture->daemon));
    g_dbus_connection_signal_unsubscribe (listener, subscription);
}

static gboolean
on_reexecute_self (GPasteDaemon *daemon G_GNUC_UNUSED,
                   gpointer      user_data)
{
    Fixture *fixture = user_data;

    fixture->stored_at_reexecution = fixture->clipboard->stores + fixture->primary->stores;
    ++fixture->reexecutions;
    return TRUE;
}

/* A re-exec has both selections stored before the host is told to go. */
static void
test_reexecute_stores_first (Fixture      *fixture,
                             gconstpointer user_data G_GNUC_UNUSED)
{
    g_signal_connect (fixture->daemon, "reexecute-self", G_CALLBACK (on_reexecute_self), fixture);
    g_paste_daemon_reexecute (fixture->daemon);
    g_paste_test_bus_wait_for_count (&fixture->reexecutions, 1);
    g_assert_cmpuint (fixture->clipboard->stores, ==, 1);
    g_assert_cmpuint (fixture->primary->stores, ==, 1);
    g_assert_cmpuint (fixture->stored_at_reexecution, ==, 2);
}

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);

    g_test_add ("/daemon/teardown/dispose-twice", Fixture, NULL, fixture_set_up, test_dispose_twice, fixture_tear_down);
    g_test_add ("/daemon/teardown/unregister-cancels-startup", Fixture, NULL, fixture_set_up, test_unregister_cancels_startup, fixture_tear_down);
    g_test_add ("/daemon/teardown/release-pending-startup", Fixture, NULL, fixture_set_up, test_release_pending_startup, fixture_tear_down);
    g_test_add ("/daemon/teardown/startup-update", Fixture, NULL, fixture_set_up, test_startup_update, fixture_tear_down);
    g_test_add ("/daemon/reexecute/stores-first", Fixture, NULL, fixture_set_up, test_reexecute_stores_first, fixture_tear_down);

    return g_paste_test_env_run ();
}
