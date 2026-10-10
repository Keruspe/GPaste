// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-env.h>

#include <signal.h>
#include <sys/resource.h>

static void
private_data_directories (void)
{
    const gchar *runtime = g_getenv ("XDG_RUNTIME_DIR");
    const gchar *variables[] = { "XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME" };

    for (guint i = 0; i < G_N_ELEMENTS (variables); ++i)
    {
        const gchar *path = g_getenv (variables[i]);

        g_assert_nonnull (path);
        g_assert_true (g_str_has_prefix (path, runtime));
        g_assert_cmpint (path[strlen (runtime)], ==, G_DIR_SEPARATOR);
        g_assert_true (g_file_test (path, G_FILE_TEST_IS_DIR));
    }
    g_assert_cmpstr (g_get_user_data_dir (), ==, g_getenv ("XDG_DATA_HOME"));
}

static void
held_connection (gconstpointer user_data)
{
    GDBusConnection *connection = (GDBusConnection *) user_data;

    g_assert_false (g_dbus_connection_is_closed (connection));
}

static void
crashed_test_reaps_bus (void)
{
    if (g_test_subprocess ())
    {
        struct rlimit limit = { 0, 0 };

        setrlimit (RLIMIT_CORE, &limit);
        raise (SIGABRT);
        g_assert_not_reached ();
    }

    /* The child runs setup before crashing, and nothing is left to remove the
     * runtime directory that makes: have it made inside ours, which teardown
     * removes. A surviving bus would hold the trap's output pipes open and hit
     * this deadline instead of reporting EOF. */
    g_auto (GStrv) envp = g_environ_setenv (g_get_environ (), "TMPDIR", g_getenv ("XDG_RUNTIME_DIR"), TRUE);

    g_test_trap_subprocess_with_envp (NULL, (const gchar * const *) envp, 3 * G_USEC_PER_SEC, G_TEST_SUBPROCESS_DEFAULT);
    g_test_trap_assert_failed ();
    g_assert_false (g_test_trap_reached_timeout ());
}

int
main (int argc, char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);

    /* GTK and GApplication may retain the shared connection past test teardown.
     * Keep a real client alive until after the server has been stopped. */
    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusConnection) connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

    g_assert_no_error (error);
    g_assert_nonnull (connection);
    g_test_add_data_func ("/environment/held-session-connection", connection, held_connection);
    g_test_add_func ("/environment/crashed-test-reaps-bus", crashed_test_reaps_bus);
    g_test_add_func ("/environment/private-data-directories", private_data_directories);
    return g_paste_test_env_run ();
}
