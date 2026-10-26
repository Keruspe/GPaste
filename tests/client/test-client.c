// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>
#include <gpaste/gpaste-gdbus-defines.h>

#include <glib-unix.h>
#include <glib/gstdio.h>

#include <signal.h>

/* gpaste-client run against a fake daemon on a private bus, which logs every
 * call it is sent: what a command line makes of its arguments shows in what
 * the client asks for. */

typedef struct
{
    GTestDBus       *bus;
    GDBusConnection *server;
    GDBusNodeInfo   *info;
    GString         *log;
    gchar           *runtime_dir;
    guint            registration;
    gboolean         refuse_reexecute;
} Fixture;

/* The uuid the fake daemon gives the item at @index: a valid one, which the
 * client checks, its last field the index's low 48 bits. */
static gchar *
uuid_at (guint64 index)
{
    return g_strdup_printf ("00000000-0000-4000-8000-%012" G_GINT64_MODIFIER "x", index & G_GUINT64_CONSTANT (0xffffffffffff));
}

static void
on_method_call (GDBusConnection       *connection G_GNUC_UNUSED,
                const gchar           *sender G_GNUC_UNUSED,
                const gchar           *path G_GNUC_UNUSED,
                const gchar           *interface,
                const gchar           *method,
                GVariant              *parameters,
                GDBusMethodInvocation *invocation,
                gpointer               user_data)
{
    Fixture *f = user_data;
    g_autofree gchar *args = g_variant_print (parameters, FALSE);

    /* With no get_property, GDBus hands the proxy's own GetAll over too. */
    if (g_str_equal (interface, "org.freedesktop.DBus.Properties"))
    {
        g_dbus_method_invocation_return_value (invocation, g_variant_new_parsed ("(@a{sv} {},)"));
        return;
    }

    g_string_append_printf (f->log, "%s%s\n", method, args);

    if (g_str_equal (method, G_PASTE_DAEMON_REEXECUTE) && f->refuse_reexecute)
        g_dbus_method_invocation_return_dbus_error (invocation, "org.freedesktop.DBus.Error.Failed", "refused");
    else if (g_str_equal (method, G_PASTE_DAEMON_GET_ELEMENT_AT_INDEX))
    {
        guint64 index;
        g_variant_get (parameters, "(t)", &index);
        g_autofree gchar *uuid = uuid_at (index);

        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(ss)", uuid, "value"));
    }
    else
        g_dbus_method_invocation_return_value (invocation, NULL);
}

static const GDBusInterfaceVTable vtable = { on_method_call, NULL, NULL, { 0 } };

static void
fixture_set_up (Fixture      *f,
                gconstpointer data G_GNUC_UNUSED)
{
    g_autoptr (GError) error = NULL;

    f->bus = g_test_dbus_new (G_TEST_DBUS_NONE);
    g_test_dbus_up (f->bus);
    f->info = g_dbus_node_info_new_for_xml (G_PASTE_DAEMON_INTERFACE, &error);
    g_assert_no_error (error);
    f->log = g_string_new (NULL);
    /* The case's own, under G_TEST_OPTION_ISOLATE_DIRS, which GLib removes
     * once the case is done. */
    f->runtime_dir = g_strdup (g_get_user_runtime_dir ());
    g_assert_cmpint (g_mkdir_with_parents (f->runtime_dir, 0700), ==, 0);
    f->server = g_paste_test_bus_new_server (f->bus, G_PASTE_DAEMON_OBJECT_PATH,
                                             f->info->interfaces[0], &vtable, f, &f->registration);
    g_assert_cmpuint (g_paste_test_bus_name_call (f->server, "RequestName",
                                                  g_variant_new ("(su)", G_PASTE_BUS_NAME, 0)), ==, 1);
}

static void
fixture_tear_down (Fixture      *f,
                   gconstpointer data G_GNUC_UNUSED)
{
    g_dbus_connection_unregister_object (f->server, f->registration);
    g_clear_object (&f->server);
    g_clear_pointer (&f->info, g_dbus_node_info_unref);
    g_string_free (f->log, TRUE);
    g_clear_pointer (&f->runtime_dir, g_free);
    g_test_dbus_down (f->bus);
    g_clear_object (&f->bus);
}

static void
on_exited (GObject      *source G_GNUC_UNUSED,
           GAsyncResult *result G_GNUC_UNUSED,
           gpointer      user_data)
{
    *((gboolean *) user_data) = TRUE;
}

/* Runs gpaste-client with @args, serving its calls meanwhile, and checks its
 * exit status and what it said on stderr. */
static void
run_client (Fixture             *f,
            const gchar * const *args,
            gboolean             success,
            const gchar         *stderr_pattern)
{
    g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
    g_autoptr (GError) error = NULL;
    gboolean exited = FALSE;

    g_strv_builder_add (builder, g_getenv ("G_PASTE_CLIENT"));
    g_strv_builder_addv (builder, (const gchar **) args);

    g_auto (GStrv) argv = g_strv_builder_end (builder);
    g_autoptr (GSubprocessLauncher) launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDERR_PIPE);

    g_subprocess_launcher_setenv (launcher, "LC_ALL", "C", TRUE);
    g_subprocess_launcher_setenv (launcher, "XDG_RUNTIME_DIR", f->runtime_dir, TRUE);

    g_autoptr (GSubprocess) client = g_subprocess_launcher_spawnv (launcher, (const gchar * const *) argv, &error);

    g_assert_no_error (error);
    g_subprocess_wait_async (client, NULL, on_exited, &exited);
    while (!exited)
        g_main_context_iteration (NULL, TRUE);

    g_autoptr (GInputStream) err = g_object_ref (g_subprocess_get_stderr_pipe (client));
    g_autoptr (GBytes) bytes = g_input_stream_read_bytes (err, 65536, NULL, &error);

    g_assert_no_error (error);

    g_autofree gchar *text = g_strndup (g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes));

    g_test_message ("stderr: %s", text);
    g_assert_cmpint (g_subprocess_get_successful (client), ==, success);
    if (stderr_pattern)
        g_assert_true (g_pattern_match_simple (stderr_pattern, text));
}

/* Anything but a complete unsigned decimal number is refused before the
 * client asks the daemon anything. */
static void
test_malformed (Fixture      *f,
                gconstpointer data G_GNUC_UNUSED)
{
    const gchar *malformed[] = { "abc", "1x", "-1", "+1", " 1", "\v-1", "", "18446744073709551616" };

    for (guint i = 0; i < G_N_ELEMENTS (malformed); ++i)
    {
        /* Past the "--", which keeps "-1" from reading as an option; the
         * index is not matched in the message, which escapes the "\v". */
        run_client (f, (const gchar * const []) { "--use-index", "delete", "--", malformed[i], NULL }, FALSE, "*Invalid item index:*");
        g_assert_cmpstr (f->log->str, ==, "");
    }
}

static void
test_index (Fixture      *f,
            gconstpointer data G_GNUC_UNUSED)
{
    run_client (f, (const gchar * const []) { "--use-index", "delete", "3", NULL }, TRUE, NULL);
    g_assert_cmpstr (f->log->str, ==, "GetElementAtIndex(3,)\nDelete('00000000-0000-4000-8000-000000000003',)\n");

    g_string_truncate (f->log, 0);
    run_client (f, (const gchar * const []) { "--use-index", "select", "18446744073709551615", NULL }, TRUE, NULL);
    g_assert_cmpstr (f->log->str, ==, "GetElementAtIndex(18446744073709551615,)\nSelect('00000000-0000-4000-8000-ffffffffffff',)\n");
}

/* A verb whose argument names no item reads no index out of it. */
static void
test_not_an_item (Fixture      *f,
                  gconstpointer data G_GNUC_UNUSED)
{
    run_client (f, (const gchar * const []) { "--use-index", "switch-history", "work", NULL }, TRUE, NULL);
    g_assert_cmpstr (f->log->str, ==, "SwitchHistory('work',)\n");
}

/* Writes the pid file the daemon leaves in the client's runtime directory. */
static void
write_pid_file (Fixture *f,
                GPid     pid)
{
    g_autofree gchar *dir = g_build_filename (f->runtime_dir, "GPaste", "Daemon", NULL);
    g_autofree gchar *path = g_build_filename (dir, "pid", NULL);
    g_autofree gchar *contents = g_strdup_printf ("%" G_PID_FORMAT, pid);

    g_assert_cmpint (g_mkdir_with_parents (dir, 0700), ==, 0);
    g_assert_true (g_file_set_contents (path, contents, -1, NULL));
}

/* A daemon refusing Reexecute is signalled only when the pid file names the
 * process owning its name: the file outlives the daemon that wrote it, and
 * its pid may be another process's since. */
static void
test_reexec_stray_pid (Fixture      *f,
                       gconstpointer data G_GNUC_UNUSED)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GSubprocess) stray = g_subprocess_new (G_SUBPROCESS_FLAGS_NONE, &error, "sleep", "60", NULL);

    g_assert_no_error (error);
    f->refuse_reexecute = TRUE;
    write_pid_file (f, (GPid) g_ascii_strtoll (g_subprocess_get_identifier (stray), NULL, 10));

    run_client (f, (const gchar * const []) { "daemon-reexec", NULL }, FALSE, NULL);

    g_subprocess_force_exit (stray);
    g_assert_true (g_subprocess_wait (stray, NULL, &error));
    g_assert_no_error (error);
    g_assert_true (g_subprocess_get_if_signaled (stray));
    g_assert_cmpint (g_subprocess_get_term_sig (stray), ==, SIGKILL);
}

static gboolean
on_wait_over (gpointer user_data)
{
    *((gboolean *) user_data) = TRUE;

    return G_SOURCE_REMOVE;
}

static gboolean
on_usr1 (gpointer user_data)
{
    *((gboolean *) user_data) = TRUE;

    return G_SOURCE_CONTINUE;
}

/* The name's owner, named by the pid file, is signalled to re-execute. */
static void
test_reexec_owner (Fixture      *f,
                   gconstpointer data G_GNUC_UNUSED)
{
    gboolean signalled = FALSE;
    guint source = g_unix_signal_add (SIGUSR1, on_usr1, &signalled);

    g_source_set_name_by_id (source, "[GPaste] SIGUSR1 listener");

    f->refuse_reexecute = TRUE;
    write_pid_file (f, getpid ());

    run_client (f, (const gchar * const []) { "daemon-reexec", NULL }, TRUE, NULL);

    /* Bounded, so a signal that never comes fails the case rather than
     * stalling the suite. */
    gboolean over = FALSE;
    guint timeout = g_timeout_add_seconds (10, on_wait_over, &over);

    g_source_set_name_by_id (timeout, "[GPaste] reexec signal watchdog");
    while (!signalled && !over)
        g_main_context_iteration (NULL, TRUE);
    g_assert_true (signalled);

    if (!over)
        g_source_remove (timeout);
    g_source_remove (source);
}

/* A pid file holding anything but a positive decimal pid names nothing: a 0
 * would have signalled the client's own process group. */
static void
test_reexec_damaged_pid_file (Fixture      *f,
                              gconstpointer data G_GNUC_UNUSED)
{
    g_autofree gchar *dir = g_build_filename (f->runtime_dir, "GPaste", "Daemon", NULL);
    g_autofree gchar *path = g_build_filename (dir, "pid", NULL);
    g_autofree gchar *own = g_strdup_printf ("%" G_PID_FORMAT "x", getpid ());

    f->refuse_reexecute = TRUE;
    g_assert_cmpint (g_mkdir_with_parents (dir, 0700), ==, 0);
    g_assert_true (g_file_set_contents (path, own, -1, NULL));

    gboolean signalled = FALSE;
    guint source = g_unix_signal_add (SIGUSR1, on_usr1, &signalled);

    g_source_set_name_by_id (source, "[GPaste] SIGUSR1 listener");

    run_client (f, (const gchar * const []) { "daemon-reexec", NULL }, FALSE, NULL);
    for (guint i = 0; i < 10; ++i)
        g_main_context_iteration (NULL, FALSE);
    g_assert_false (signalled);

    g_source_remove (source);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

    g_test_add ("/client/use-index/malformed", Fixture, NULL, fixture_set_up, test_malformed, fixture_tear_down);
    g_test_add ("/client/use-index/index", Fixture, NULL, fixture_set_up, test_index, fixture_tear_down);
    g_test_add ("/client/use-index/not-an-item", Fixture, NULL, fixture_set_up, test_not_an_item, fixture_tear_down);
    g_test_add ("/client/reexec/stray-pid", Fixture, NULL, fixture_set_up, test_reexec_stray_pid, fixture_tear_down);
    g_test_add ("/client/reexec/owner", Fixture, NULL, fixture_set_up, test_reexec_owner, fixture_tear_down);
    g_test_add ("/client/reexec/damaged-pid-file", Fixture, NULL, fixture_set_up, test_reexec_damaged_pid_file, fixture_tear_down);

    return g_test_run ();
}
