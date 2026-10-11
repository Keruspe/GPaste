// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-env.h>

#include <errno.h>
#include <fcntl.h>
#include <glib-unix.h>
#include <signal.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

static gchar *tmp_dir;
static gchar *runtime_dir;
static GSubprocess *supervisor;
static GSubprocess *xvfb;

/* dbus-daemon refuses a socket path longer than 99 characters, and our bus makes
 * its own as "/dbus-" and ten more characters in "/gpaste-test-runtime-XXXXXX"
 * under the temporary directory. A fixture's GTestDBus always listens in /tmp. */
#define G_PASTE_TEST_TMPDIR_MAX (99 - 16 - 27)

static void
die_with_parent (gpointer user_data G_GNUC_UNUSED)
{
#ifdef __linux__
    prctl (PR_SET_PDEATHSIG, SIGTERM);
#endif
}

/* No activation directories: a test bus must not start desktop services. A
 * subprocess also avoids GTestDBus finalization waiting for GIO's singleton.
 * Without a bus, the supervisor is only there to remove the directories. */
static GSubprocess *
start_supervisor (gboolean with_bus)
{
    g_autoptr (GError) error = NULL;
    g_autofree gchar *config_path = NULL;

    if (with_bus)
    {
        config_path = g_build_filename (runtime_dir, "bus.conf", NULL);
        g_autofree gchar *escaped = g_markup_escape_text (runtime_dir, -1);
        g_autofree gchar *config = g_strdup_printf ("<busconfig><type>session</type>"
                                                    "<listen>unix:tmpdir=%s</listen>"
                                                    "<policy context=\"default\"><allow send_destination=\"*\"/>"
                                                    "<allow receive_sender=\"*\"/><allow own=\"*\"/>"
                                                    "</policy></busconfig>", escaped);

        if (!g_file_set_contents (config_path, config, -1, &error))
            g_error ("Could not configure the test bus: %s", error->message);
    }

    g_autoptr (GSubprocessLauncher) launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE);
    g_autoptr (GSubprocess) process = g_subprocess_launcher_spawn (launcher, &error,
                                                                  G_PASTE_TEST_BUS_SUPERVISOR,
                                                                  config_path ? config_path : "",
                                                                  tmp_dir ? tmp_dir : runtime_dir, NULL);
    if (!process)
        g_error ("Could not start the test bus supervisor: %s", error->message);

    if (with_bus)
    {
        g_autoptr (GDataInputStream) output = g_data_input_stream_new (g_subprocess_get_stdout_pipe (process));
        g_autofree gchar *address = g_data_input_stream_read_line (output, NULL, NULL, &error);

        if (!address || !*address)
            g_error ("The test bus did not announce an address: %s", error ? error->message : "end of stream");

        g_setenv ("DBUS_SESSION_BUS_ADDRESS", address, TRUE);
        g_setenv ("DBUS_SYSTEM_BUS_ADDRESS", address, TRUE);
    }
    return g_steal_pointer (&process);
}

static GSubprocess *
start_xvfb (void)
{
    g_autofree gchar *program = g_find_program_in_path ("Xvfb");
    if (!program)
        return NULL;

    g_autoptr (GError) error = NULL;
    gint fds[2];
    if (!g_unix_open_pipe (fds, O_CLOEXEC, &error))
        g_error ("Could not create the Xvfb display pipe: %s", error->message);

    /* The launcher owns the write end and closes it when freed, so a server that
     * dies before announcing its display ends the read below instead of hanging
     * it. */
    g_autoptr (GSubprocessLauncher) launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE);
    g_subprocess_launcher_take_fd (launcher, fds[1], 3);
    g_subprocess_launcher_set_child_setup (launcher, die_with_parent, NULL, NULL);
    g_autoptr (GSubprocess) server = g_subprocess_launcher_spawn (launcher, &error, program,
                                                                  "-displayfd", "3",
                                                                  "-nolisten", "tcp",
                                                                  "-terminate",
                                                                  "-screen", "0", "1280x1024x24",
                                                                  NULL);
    g_clear_object (&launcher);
    if (!server)
        g_error ("Could not start Xvfb: %s", error->message);

    g_autoptr (GString) display = g_string_new (":");
    gchar c;
    while (read (fds[0], &c, 1) == 1 && c != '\n')
        g_string_append_c (display, c);
    close (fds[0]);
    if (display->len == 1)
        g_error ("Xvfb exited without announcing a display");

    g_setenv ("DISPLAY", display->str, TRUE);
    g_setenv ("GDK_BACKEND", "x11", TRUE);

    return g_steal_pointer (&server);
}

/**
 * g_paste_test_env_setup:
 * @flags: what the binary needs besides private settings and runtime directory
 *
 * Detach the process from the user's session before anything reads it: memory
 * GSettings on the build tree's schemas, private runtime, data, configuration
 * and cache directories, a private bus, no display but a private Xvfb. Call it
 * first in main(), before gtk_init_check() and g_test_init(), so that a binary
 * run by hand is as harmless as one run by meson.
 */
void
g_paste_test_env_setup (GPasteTestEnvFlags flags)
{
    g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);
    g_setenv ("GSETTINGS_SCHEMA_DIR", G_PASTE_TEST_SCHEMA_DIR, TRUE);

    const gchar *session[] = {
        "DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "XAUTHORITY", "AT_SPI_BUS_ADDRESS",
        "DBUS_SESSION_BUS_ADDRESS", "DBUS_SYSTEM_BUS_ADDRESS", "DBUS_STARTER_ADDRESS", "DBUS_STARTER_BUS_TYPE",
    };
    for (guint i = 0; i < G_N_ELEMENTS (session); ++i)
        g_unsetenv (session[i]);

    /* A long temporary directory -- a package build tree's $TMPDIR, or the
     * $G_TEST_TMPDIR a trap child gets inside its parent's, which GLib prefers
     * -- leaves no room for a bus socket: the tests then get a short one of
     * their own, before GLib caches where it is. */
    const gchar *variable = g_getenv ("G_TEST_TMPDIR") ? "G_TEST_TMPDIR" : "TMPDIR";
    const gchar *tmp = g_getenv (variable);

    if (tmp && strlen (tmp) > G_PASTE_TEST_TMPDIR_MAX)
    {
        tmp_dir = g_strdup ("/tmp/gpaste-test-XXXXXX");
        if (!g_mkdtemp_full (tmp_dir, 0700))
            g_error ("Could not create a temporary directory: %s", g_strerror (errno));
        g_setenv (variable, tmp_dir, TRUE);
    }

    /* GIO looks for a session bus at $XDG_RUNTIME_DIR/bus when no address is
     * set. */
    g_autoptr (GError) error = NULL;
    runtime_dir = g_dir_make_tmp ("gpaste-test-runtime-XXXXXX", &error);
    if (!runtime_dir)
        g_error ("Could not create a runtime directory: %s", error->message);
    g_setenv ("XDG_RUNTIME_DIR", runtime_dir, TRUE);

    /* Data, configuration and cache go beneath it too, before GLib caches where
     * they are; a daemon the test spawns inherits them, unless g_test_init () is
     * given G_TEST_OPTION_ISOLATE_DIRS, which points them at /dev/null. */
    static const struct
    {
        const gchar *variable;
        const gchar *directory;
    } xdg[] = {
        { "XDG_DATA_HOME",   "data"   },
        { "XDG_CONFIG_HOME", "config" },
        { "XDG_CACHE_HOME",  "cache"  },
    };

    for (guint i = 0; i < G_N_ELEMENTS (xdg); ++i)
    {
        g_autofree gchar *path = g_build_filename (runtime_dir, xdg[i].directory, NULL);

        if (g_mkdir_with_parents (path, 0700) < 0)
            g_error ("Could not create a private test directory: %s", path);
        g_setenv (xdg[i].variable, path, TRUE);
    }

    g_setenv ("GTK_A11Y", "none", TRUE);
    g_setenv ("GIO_USE_VFS", "local", TRUE);
    g_setenv ("ADW_DISABLE_PORTAL", "1", TRUE);

    if (flags & G_PASTE_TEST_ENV_OWN_BUS)
    {
        g_autofree gchar *nowhere = g_strdup_printf ("unix:path=%s/no-bus", runtime_dir);
        g_setenv ("DBUS_SESSION_BUS_ADDRESS", nowhere, TRUE);
        g_setenv ("DBUS_SYSTEM_BUS_ADDRESS", nowhere, TRUE);
    }

    /* dbus-daemon is a test dependency, even for model-only suites: library
     * initialization may acquire GIO's shared bus. Fail closed rather than
     * silently skipping isolation or changing behavior with installed tools.
     * OWN_BUS reserves isolation for fixtures that manage their own bus. */
    supervisor = start_supervisor (!(flags & G_PASTE_TEST_ENV_OWN_BUS));

    if (flags & G_PASTE_TEST_ENV_DISPLAY)
        xvfb = start_xvfb ();
}

gboolean
g_paste_test_env_has_display (void)
{
    return xvfb != NULL;
}

/**
 * g_paste_test_env_run:
 *
 * Run the tests, then stop the bus g_paste_test_env_setup() started and remove
 * its directories.
 *
 * Returns: the result of g_test_run()
 */
int
g_paste_test_env_run (void)
{
    int result = g_test_run ();

    /* Xvfb is left running: GDK exits the process as soon as its display goes
     * away. -terminate ends the server once the process closes that connection
     * at exit, and PR_SET_PDEATHSIG if it never opened one. */
    g_clear_object (&xvfb);
    /* Stop and reap the server independently of any live client connections;
     * the supervisor removes the directories once it has. Say so rather than
     * close the lifeline: a process forked without exec, as GTestDBus forks its
     * watcher, holds a copy of it until we exit. */
    GOutputStream *lifeline = g_subprocess_get_stdin_pipe (supervisor);

    g_output_stream_write_all (lifeline, "\n", 1, NULL, NULL, NULL);
    g_output_stream_close (lifeline, NULL, NULL);
    g_subprocess_wait (supervisor, NULL, NULL);
    g_clear_object (&supervisor);
    g_clear_pointer (&runtime_dir, g_free);
    g_clear_pointer (&tmp_dir, g_free);

    return result;
}
