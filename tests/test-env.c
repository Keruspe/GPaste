// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-env.h>

#include <glib/gstdio.h>
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

static gboolean
runtime_dirs_left (const gchar *parent)
{
    g_autoptr (GDir) dir = g_dir_open (parent, 0, NULL);
    const gchar *name;

    while (dir && (name = g_dir_read_name (dir)))
    {
        if (g_str_has_prefix (name, "gpaste-test-runtime-"))
            return TRUE;
    }
    return FALSE;
}

/* What a child that got through its setup prints: a bus to talk to, and where
 * its directories are. */
static void
announce_setup (void)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusConnection) connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

    g_assert_no_error (error);
    g_print ("set up in %s\n", g_getenv ("XDG_RUNTIME_DIR"));
}

static void
crashed_test_reaps_bus (void)
{
    if (g_test_subprocess ())
    {
        struct rlimit limit = { 0, 0 };

        announce_setup ();
        setrlimit (RLIMIT_CORE, &limit);
        raise (SIGABRT);
        g_assert_not_reached ();
    }

    /* The child runs setup before crashing, and leaves its directories to its
     * supervisor: have them made in a directory of ours, where they can be
     * checked for. One short enough for the child to keep, which ours, under
     * a long TMPDIR, would not be: a replacement in /tmp would be checked for
     * nowhere. A surviving bus would hold the trap's output pipes open and hit
     * this deadline instead of reporting EOF. */
    g_autofree gchar *tmpdir = g_mkdtemp (g_strdup ("/tmp/gpaste-test-crash-XXXXXX"));
    g_auto (GStrv) envp = g_environ_setenv (g_get_environ (), "TMPDIR", tmpdir, TRUE);

    g_assert_nonnull (tmpdir);
    g_test_trap_subprocess_with_envp (NULL, (const gchar * const *) envp, 3 * G_USEC_PER_SEC, G_TEST_SUBPROCESS_DEFAULT);
    g_test_trap_assert_failed ();
    g_test_trap_assert_stdout ("set up in /tmp/gpaste-test-crash-*/gpaste-test-runtime-*");
    g_assert_false (g_test_trap_reached_timeout ());
    g_assert_false (runtime_dirs_left (tmpdir));
    g_assert_cmpint (g_rmdir (tmpdir), ==, 0);
}

/* This binary, to run one of its cases as a child whose output we keep:
 * g_test_trap_subprocess () keeps it from us. */
static gchar *self_path;

/* A child set up under a temporary directory deeper than a socket path can be,
 * as a package build tree's often is, in @variable. */
static void
long_tmpdir_in (const gchar *variable)
{
    g_autofree gchar *tmpdir = g_build_filename (g_getenv ("XDG_RUNTIME_DIR"),
                                                 "a-build-tree-whose-temporary-directory-is-deeper-than-a-socket-path", NULL);
    g_autoptr (GSubprocessLauncher) launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE);
    g_autoptr (GError) error = NULL;
    g_autofree gchar *output = NULL;

    g_assert_cmpint (g_mkdir_with_parents (tmpdir, 0700), ==, 0);
    g_subprocess_launcher_setenv (launcher, variable, tmpdir, TRUE);

    g_autoptr (GSubprocess) child = g_subprocess_launcher_spawn (launcher, &error, self_path, "--GTestSubprocess",
                                                                 "-q", "-p", "/environment/long-tmpdir", NULL);

    g_assert_no_error (error);
    g_assert_true (g_subprocess_communicate_utf8 (child, NULL, NULL, &output, NULL, &error));
    g_assert_no_error (error);
    g_assert_true (g_subprocess_get_if_exited (child));
    g_assert_cmpint (g_subprocess_get_exit_status (child), ==, 0);

    /* The short directory it was given instead is gone with it. */
    g_autofree gchar *runtime = g_strstrip (g_strdup (output));
    g_autofree gchar *replacement = NULL;

    g_assert_true (g_pattern_match_simple ("set up in /tmp/gpaste-test-*/gpaste-test-runtime-*", runtime));
    replacement = g_path_get_dirname (runtime + strlen ("set up in "));
    g_assert_false (g_file_test (replacement, G_FILE_TEST_EXISTS));
}

static void
long_tmpdir (void)
{
    if (g_test_subprocess ())
    {
        announce_setup ();
        return;
    }

    long_tmpdir_in ("TMPDIR");
    /* What a trap child of a G_TEST_OPTION_ISOLATE_DIRS suite gets. */
    long_tmpdir_in ("G_TEST_TMPDIR");
}

/* A bus that cannot be started still has the directories removed: the test
 * dies on the address it never gets, leaving them to the supervisor. */
static void
unstarted_bus (void)
{
    g_autofree gchar *directory = g_build_filename (g_getenv ("XDG_RUNTIME_DIR"), "unstarted-bus", NULL);
    g_autofree gchar *config = g_build_filename (directory, "bus.conf", NULL);
    g_autoptr (GSubprocessLauncher) launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                                                          G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                                                          G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    g_autoptr (GError) error = NULL;

    g_assert_cmpint (g_mkdir_with_parents (directory, 0700), ==, 0);
    g_assert_true (g_file_set_contents (config, "<busconfig/>", -1, NULL));
    g_subprocess_launcher_setenv (launcher, "PATH", "/nonexistent", TRUE);

    g_autoptr (GSubprocess) supervisor = g_subprocess_launcher_spawn (launcher, &error, G_PASTE_TEST_BUS_SUPERVISOR,
                                                                      config, directory, NULL);

    g_assert_no_error (error);
    g_assert_true (g_subprocess_wait (supervisor, NULL, &error));
    g_assert_no_error (error);
    g_assert_cmpint (g_subprocess_get_exit_status (supervisor), ==, 1);
    g_assert_false (g_file_test (directory, G_FILE_TEST_EXISTS));
}

int
main (int argc, char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    self_path = g_canonicalize_filename (argv[0], NULL);
    g_test_init (&argc, &argv, NULL);

    /* GTK and GApplication may retain the shared connection past test teardown.
     * Keep a real client alive until after the server has been stopped. */
    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusConnection) connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

    g_assert_no_error (error);
    g_assert_nonnull (connection);
    g_test_add_data_func ("/environment/held-session-connection", connection, held_connection);
    g_test_add_func ("/environment/crashed-test-reaps-bus", crashed_test_reaps_bus);
    g_test_add_func ("/environment/long-tmpdir", long_tmpdir);
    g_test_add_func ("/environment/private-data-directories", private_data_directories);
    g_test_add_func ("/environment/unstarted-bus", unstarted_bus);
    return g_paste_test_env_run ();
}
