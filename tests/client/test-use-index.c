// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>
#include <gpaste/gpaste-gdbus-defines.h>

/* gpaste-client run against a fake daemon on a private bus, which logs every
 * call it is sent: what --use-index makes of an argument shows in what the
 * client asks for. */

typedef struct
{
    GTestDBus       *bus;
    GDBusConnection *server;
    GDBusNodeInfo   *info;
    GString         *log;
    guint            registration;
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

    if (g_str_equal (method, G_PASTE_DAEMON_GET_ELEMENT_AT_INDEX))
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

/* Runs gpaste-client with --use-index and @args, serving its calls meanwhile,
 * and checks its exit status and what it said on stderr. */
static void
run_client (const gchar * const *args,
            gboolean             success,
            const gchar         *stderr_pattern)
{
    g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
    g_autoptr (GError) error = NULL;
    gboolean exited = FALSE;

    g_strv_builder_add (builder, g_getenv ("G_PASTE_CLIENT"));
    g_strv_builder_add (builder, "--use-index");
    g_strv_builder_addv (builder, (const gchar **) args);

    g_auto (GStrv) argv = g_strv_builder_end (builder);
    g_autoptr (GSubprocessLauncher) launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDERR_PIPE);

    g_subprocess_launcher_setenv (launcher, "LC_ALL", "C", TRUE);

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
        run_client ((const gchar * const []) { "delete", "--", malformed[i], NULL }, FALSE, "*Invalid item index:*");
        g_assert_cmpstr (f->log->str, ==, "");
    }
}

static void
test_index (Fixture      *f,
            gconstpointer data G_GNUC_UNUSED)
{
    run_client ((const gchar * const []) { "delete", "3", NULL }, TRUE, NULL);
    g_assert_cmpstr (f->log->str, ==, "GetElementAtIndex(3,)\nDelete('00000000-0000-4000-8000-000000000003',)\n");

    g_string_truncate (f->log, 0);
    run_client ((const gchar * const []) { "select", "18446744073709551615", NULL }, TRUE, NULL);
    g_assert_cmpstr (f->log->str, ==, "GetElementAtIndex(18446744073709551615,)\nSelect('00000000-0000-4000-8000-ffffffffffff',)\n");
}

/* A verb whose argument names no item reads no index out of it. */
static void
test_not_an_item (Fixture      *f,
                  gconstpointer data G_GNUC_UNUSED)
{
    run_client ((const gchar * const []) { "switch-history", "work", NULL }, TRUE, NULL);
    g_assert_cmpstr (f->log->str, ==, "SwitchHistory('work',)\n");
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add ("/client/use-index/malformed", Fixture, NULL, fixture_set_up, test_malformed, fixture_tear_down);
    g_test_add ("/client/use-index/index", Fixture, NULL, fixture_set_up, test_index, fixture_tear_down);
    g_test_add ("/client/use-index/not-an-item", Fixture, NULL, fixture_set_up, test_not_an_item, fixture_tear_down);

    return g_test_run ();
}
