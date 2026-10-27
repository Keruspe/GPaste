// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* g_paste_util_spawn () against a real bus, in a process of its own that holds
 * no other reference to it: the call reaches an app that is there, with the
 * activation token it is handed, and one the bus cannot start is warned
 * about. */

#include <gpaste-test-bus.h>
#include <gpaste-test-env.h>
#include <gpaste-3/gpaste-util.h>
#include <gpaste-3/gpaste-util-private.h>

static const gchar app_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.Application'>"
    "    <method name='Activate'><arg type='a{sv}' name='platform_data' direction='in'/></method>"
    "  </interface>"
    "</node>";

/* The activation token the last Activate came with, "(none)" for none. */
static void
on_app_call (GDBusConnection       *connection     G_GNUC_UNUSED,
             const gchar           *sender         G_GNUC_UNUSED,
             const gchar           *object_path    G_GNUC_UNUSED,
             const gchar           *interface_name G_GNUC_UNUSED,
             const gchar           *method_name,
             GVariant              *parameters,
             GDBusMethodInvocation *invocation,
             gpointer               user_data)
{
    gchar **activated = user_data;

    if (g_paste_str_equal (method_name, "Activate"))
    {
        g_autoptr (GVariant) platform_data = g_variant_get_child_value (parameters, 0);
        const gchar *token = "(none)";

        g_variant_lookup (platform_data, "activation-token", "&s", &token);
        g_set_str (activated, token);
    }
    g_dbus_method_invocation_return_value (invocation, NULL);
}

/* The app owns its name on a connection of its own, so that the session bus
 * g_paste_util_spawn () gets is held by nothing but the call. */
static void
test_spawn_reaches_app (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 30 * G_USEC_PER_SEC, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusNodeInfo) info = g_dbus_node_info_new_for_xml (app_xml, &error);
    g_autoptr (GDBusConnection) app = g_paste_test_bus_connect (g_getenv ("DBUS_SESSION_BUS_ADDRESS"));
    static const GDBusInterfaceVTable vtable = { on_app_call, NULL, NULL, { 0 } };
    g_autofree gchar *activated = NULL;

    g_assert_no_error (error);

    guint registration = g_dbus_connection_register_object (app, "/org/gnome/GPaste/Ui", info->interfaces[0], &vtable, &activated, NULL, &error);

    g_assert_no_error (error);

    guint32 owned = g_paste_test_bus_name_call (app, "RequestName", g_variant_new ("(su)", "org.gnome.GPaste.Ui", 0));

    g_assert_cmpuint (owned, ==, 1 /* primary owner */);
    g_unsetenv ("XDG_ACTIVATION_TOKEN");
    g_paste_util_spawn ("Ui");
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &activated, NULL);
    g_assert_cmpstr (activated, ==, "(none)");

    /* The daemon's, handed by the portal with a shortcut. */
    g_clear_pointer (&activated, g_free);
    g_paste_util_spawn_with_token ("Ui", "token");
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &activated, NULL);
    g_assert_cmpstr (activated, ==, "token");

    g_dbus_connection_unregister_object (app, registration);
}

static guint failures_warned;

/* Counts the warning a call the bus cannot start gives, which goes on to
 * stderr for the parent to check. */
static GLogWriterOutput
count_failures_warned (GLogLevelFlags   log_level,
                       const GLogField *fields,
                       gsize            n_fields,
                       gpointer         user_data)
{
    for (gsize i = 0; i < n_fields; ++i)
    {
        if (g_paste_str_equal (fields[i].key, "MESSAGE") && fields[i].length < 0 &&
            g_str_has_prefix (fields[i].value, "Failed to call org.gnome.GPaste.Ui"))
            ++failures_warned;
    }

    return g_log_writer_default (log_level, fields, n_fields, user_data);
}

/* No service file on the test bus: the bus answers that it has nothing to
 * start, which is all there is to say why nothing opened. */
static void
test_spawn_failure_warned (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 30 * G_USEC_PER_SEC, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*Failed to call org.gnome.GPaste.Ui:*");
        return;
    }

    g_log_set_writer_func (count_failures_warned, NULL, NULL);
    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);
    g_paste_util_spawn ("Ui");
    g_paste_test_bus_wait_for_count (&failures_warned, 1);
}

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/util/spawn/reaches-app", test_spawn_reaches_app);
    g_test_add_func ("/util/spawn/failure-warned", test_spawn_failure_warned);

    return g_paste_test_env_run ();
}
