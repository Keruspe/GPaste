// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>
#include <gpaste-test-env.h>
#include <gpaste-3/gpaste-util.h>
#include <gpaste-3/gpaste-gsettings-keys.h>

#define G_SETTINGS_ENABLE_BACKEND 1
#include <gio/gsettingsbackend.h>

static GSettingsBackend *shell_backend;

static GSettings *
test_settings_new (const gchar *name)
{
    if (!g_str_equal (name, G_PASTE_SHELL_SETTINGS_NAME))
        return g_settings_new (name);

    g_autoptr (GError) error = NULL;
    g_autoptr (GSettingsSchemaSource) source = g_settings_schema_source_new_from_directory (G_PASTE_TEST_SHELL_SCHEMA_DIR, NULL, FALSE, &error);

    g_assert_no_error (error);

    g_autoptr (GSettingsSchema) schema = g_settings_schema_source_lookup (source, name, FALSE);

    g_assert_nonnull (schema);
    return g_settings_new_full (schema, shell_backend, NULL);
}

static gboolean
test_has_shell (void)
{
    return TRUE;
}

/* Supply only the Shell schema and backend; all reads, writes and notifications
 * use GSettings, including its non-writable backend's refused writes. */
#define g_settings_new test_settings_new
#define g_paste_util_has_gnome_shell test_has_shell
#include "../../src/libgpaste/gpaste-3/gpaste-settings.c"
#undef g_paste_util_has_gnome_shell
#undef g_settings_new

static void
test_extension_write (gconstpointer data)
{
    gboolean writable = GPOINTER_TO_INT (data);
    g_autoptr (GSettingsBackend) backend = (writable) ? g_memory_settings_backend_new () : g_null_settings_backend_new ();

    shell_backend = backend;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GSettings) shell = test_settings_new (G_PASTE_SHELL_SETTINGS_NAME);
    guint notifications = 0;

    g_signal_connect_swapped (settings, "notify::" G_PASTE_EXTENSION_ENABLED_SETTING, G_CALLBACK (g_paste_test_bus_count_emission), &notifications);

    g_assert_cmpint (g_settings_is_writable (shell, G_PASTE_SHELL_ENABLED_EXTENSIONS_SETTING), ==, writable);
    g_assert_false (g_paste_settings_get_extension_enabled (settings));
    g_paste_settings_set_extension_enabled (settings, TRUE);

    g_auto (GStrv) enabled = g_settings_get_strv (shell, G_PASTE_SHELL_ENABLED_EXTENSIONS_SETTING);

    g_assert_cmpint (g_strv_contains ((const gchar * const *) enabled, G_PASTE_EXTENSION_NAME), ==, writable);
    g_assert_cmpint (g_paste_settings_get_extension_enabled (settings), ==, writable);
    g_assert_cmpuint (notifications, ==, writable);

    if (writable)
    {
        /* Another extension coming says nothing about GPaste's. */
        const gchar *others[] = { G_PASTE_EXTENSION_NAME, "other-extension", NULL };

        g_assert_true (g_settings_set_strv (shell, G_PASTE_SHELL_ENABLED_EXTENSIONS_SETTING, others));
        g_assert_cmpuint (notifications, ==, 1);
        g_paste_settings_set_extension_enabled (settings, FALSE);

        g_auto (GStrv) remaining = g_settings_get_strv (shell, G_PASTE_SHELL_ENABLED_EXTENSIONS_SETTING);

        g_assert_cmpstr (remaining[0], ==, "other-extension");
        g_assert_null (remaining[1]);
        g_assert_false (g_paste_settings_get_extension_enabled (settings));
        g_assert_cmpuint (notifications, ==, 2);
    }
    shell_backend = NULL;
}

/* What the preferences' extension switch does: a refused flip has to come back,
 * though GBinding ignores the notify of the object it is writing to. */
static void
test_extension_binding (void)
{
    g_autoptr (GSettingsBackend) backend = g_null_settings_backend_new ();

    shell_backend = backend;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GSimpleAction) row = g_simple_action_new ("row", NULL);

    g_object_bind_property (settings, G_PASTE_EXTENSION_ENABLED_SETTING, row, "enabled", G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
    g_assert_false (g_action_get_enabled (G_ACTION (row)));

    g_simple_action_set_enabled (row, TRUE);
    while (g_main_context_iteration (NULL, FALSE));

    g_assert_false (g_paste_settings_get_extension_enabled (settings));
    g_assert_false (g_action_get_enabled (G_ACTION (row)));
    shell_backend = NULL;
}

/* Whether the extension is enabled is what GNOME Shell decides from its three
 * keys: listed as disabled wins over listed as enabled, and user extensions
 * turned off turn it off too; a change to any of them is announced. Enabling
 * it puts it in the one list and takes it out of the other, as the shell
 * does, the global switch staying the user's. */
static void
test_extension_disabled_wins (void)
{
    g_autoptr (GSettingsBackend) backend = g_memory_settings_backend_new ();

    shell_backend = backend;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GSettings) shell = test_settings_new (G_PASTE_SHELL_SETTINGS_NAME);
    const gchar *gpaste[] = { G_PASTE_EXTENSION_NAME, NULL };
    const gchar *none[] = { NULL };
    guint notifications = 0;

    g_signal_connect_swapped (settings, "notify::" G_PASTE_EXTENSION_ENABLED_SETTING, G_CALLBACK (g_paste_test_bus_count_emission), &notifications);

    g_assert_true (g_settings_set_strv (shell, G_PASTE_SHELL_ENABLED_EXTENSIONS_SETTING, gpaste));
    g_assert_true (g_paste_settings_get_extension_enabled (settings));
    g_assert_true (g_settings_set_strv (shell, G_PASTE_SHELL_DISABLED_EXTENSIONS_SETTING, gpaste));
    g_assert_false (g_paste_settings_get_extension_enabled (settings));
    g_assert_cmpuint (notifications, ==, 2);

    g_paste_settings_set_extension_enabled (settings, TRUE);
    g_assert_true (g_paste_settings_get_extension_enabled (settings));

    g_auto (GStrv) disabled = g_settings_get_strv (shell, G_PASTE_SHELL_DISABLED_EXTENSIONS_SETTING);

    g_assert_false (g_strv_contains ((const gchar * const *) disabled, G_PASTE_EXTENSION_NAME));

    g_assert_true (g_settings_set_boolean (shell, G_PASTE_SHELL_DISABLE_USER_EXTENSIONS_SETTING, TRUE));
    g_assert_false (g_paste_settings_get_extension_enabled (settings));
    g_paste_settings_set_extension_enabled (settings, TRUE);
    g_assert_true (g_settings_get_boolean (shell, G_PASTE_SHELL_DISABLE_USER_EXTENSIONS_SETTING));
    g_assert_false (g_paste_settings_get_extension_enabled (settings));
    g_assert_true (g_settings_set_boolean (shell, G_PASTE_SHELL_DISABLE_USER_EXTENSIONS_SETTING, FALSE));
    g_assert_true (g_paste_settings_get_extension_enabled (settings));

    g_paste_settings_set_extension_enabled (settings, FALSE);
    g_assert_false (g_paste_settings_get_extension_enabled (settings));

    g_auto (GStrv) enabled = g_settings_get_strv (shell, G_PASTE_SHELL_ENABLED_EXTENSIONS_SETTING);
    g_auto (GStrv) disabled_again = g_settings_get_strv (shell, G_PASTE_SHELL_DISABLED_EXTENSIONS_SETTING);

    g_assert_false (g_strv_contains ((const gchar * const *) enabled, G_PASTE_EXTENSION_NAME));
    g_assert_true (g_strv_contains ((const gchar * const *) disabled_again, G_PASTE_EXTENSION_NAME));

    g_assert_true (g_settings_set_strv (shell, G_PASTE_SHELL_DISABLED_EXTENSIONS_SETTING, none));
    shell_backend = NULL;
}

/* A reload reads whether the extension is enabled as well, and says so once
 * when it moved: GNOME Shell's change may not have reached this process yet. */
static void
test_extension_reload (void)
{
    g_autoptr (GSettingsBackend) backend = g_memory_settings_backend_new ();

    shell_backend = backend;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GSettings) shell = test_settings_new (G_PASTE_SHELL_SETTINGS_NAME);
    const gchar *gpaste[] = { G_PASTE_EXTENSION_NAME, NULL };
    const gchar *none[] = { NULL };
    guint notifications = 0;

    g_signal_connect_swapped (settings, "notify::" G_PASTE_EXTENSION_ENABLED_SETTING, G_CALLBACK (g_paste_test_bus_count_emission), &notifications);

    /* The change the shell's write raises held back, as one still on its way. */
    g_signal_group_block (settings->shell_settings_signals);
    g_assert_true (g_settings_set_strv (shell, G_PASTE_SHELL_ENABLED_EXTENSIONS_SETTING, gpaste));
    g_signal_group_unblock (settings->shell_settings_signals);
    g_assert_false (g_paste_settings_get_extension_enabled (settings));

    g_paste_settings_reload (settings);
    g_assert_true (g_paste_settings_get_extension_enabled (settings));
    g_assert_cmpuint (notifications, ==, 1);

    g_assert_true (g_settings_set_strv (shell, G_PASTE_SHELL_ENABLED_EXTENSIONS_SETTING, none));
    shell_backend = NULL;
}

int
main (int argc, char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_OWN_BUS);
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_data_func ("/settings/extension/rejected-write", GINT_TO_POINTER (FALSE), test_extension_write);
    g_test_add_data_func ("/settings/extension/accepted-write", GINT_TO_POINTER (TRUE), test_extension_write);
    g_test_add_func ("/settings/extension/refused-binding", test_extension_binding);
    g_test_add_func ("/settings/extension/disabled-wins", test_extension_disabled_wins);
    g_test_add_func ("/settings/extension/reload", test_extension_reload);
    return g_paste_test_env_run ();
}
