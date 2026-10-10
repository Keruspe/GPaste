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

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);

    g_test_add ("/daemon/teardown/dispose-twice", Fixture, NULL, fixture_set_up, test_dispose_twice, fixture_tear_down);

    return g_paste_test_env_run ();
}
