// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>
#include <gpaste-test-env.h>
#include <gpaste-3/gpaste-settings.h>
#include <gpaste-3/gpaste-util.h>
#include <gpaste-3/gpaste-gsettings-keys.h>

typedef struct
{
    const gchar *key;
    guint emissions;
} Rebind;

static void
on_rebind (GPasteSettings *settings G_GNUC_UNUSED,
           const gchar    *key,
           gpointer        user_data)
{
    Rebind *rebind = user_data;
    g_assert_cmpstr (key, ==, rebind->key);
    ++rebind->emissions;
}

static void
rebind_key (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    Rebind ordinary = { .key = "launch-ui" };
    Rebind detailed = { .key = "launch-ui" };
    g_signal_connect (settings, "rebind", G_CALLBACK (on_rebind), &ordinary);
    g_signal_connect (settings, "rebind::launch-ui", G_CALLBACK (on_rebind), &detailed);

    g_paste_settings_set_launch_ui (settings, "<Control><Alt>F11");
    g_assert_cmpuint (ordinary.emissions, ==, 1);
    g_assert_cmpuint (detailed.emissions, ==, 1);

    ordinary.key = "pop";
    g_paste_settings_set_pop (settings, "<Control><Alt>F12");
    g_assert_cmpuint (ordinary.emissions, ==, 2);
    g_assert_cmpuint (detailed.emissions, ==, 1);
}

static void
migration_gate (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_storage_backend_revision (settings, 37);
    guint64 revision = g_paste_util_prepare_storage_migration ();

    g_assert_cmpuint (revision, ==, 37);
    g_assert_cmpuint (g_paste_settings_get_storage_backend_revision (settings), ==, 0);
    g_paste_util_cancel_storage_migration (revision);
    g_assert_cmpuint (g_paste_settings_get_storage_backend_revision (settings), ==, 37);

    revision = g_paste_util_prepare_storage_migration ();
    /* A completed migration's revision wins over a late failed reply. */
    g_paste_settings_set_storage_backend_revision (settings, 42);
    g_paste_util_cancel_storage_migration (revision);
    g_assert_cmpuint (g_paste_settings_get_storage_backend_revision (settings), ==, 42);

    /* A gate that was open because the key had no value at all closes back to
     * having none: prepare () opens it by resetting the key, so cancelling has to
     * leave that same state and not a user value
     * that merely equals the default. */
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_REVISION_SETTING);
    revision = g_paste_util_prepare_storage_migration ();
    g_assert_cmpuint (revision, ==, 0);
    g_paste_util_cancel_storage_migration (revision);
    g_assert_true (g_paste_settings_is_default (settings, G_PASTE_STORAGE_BACKEND_REVISION_SETTING));

    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_REVISION_SETTING);
}

/* A value out of the schema's range is refused by GSettings itself, as a locked
 * key is: the setting keeps what it held, which is not its default here, so a
 * refusal resetting the key would show. */
static void
rejected_write (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    guint notifications = 0;

    g_paste_settings_set_max_history_size (settings, 250);
    g_signal_connect_swapped (settings, "notify::" G_PASTE_MAX_HISTORY_SIZE_SETTING, G_CALLBACK (g_paste_test_bus_count_emission), &notifications);

    g_test_expect_message ("GLib-GIO", G_LOG_LEVEL_WARNING, "*max-history-size*outside of valid range*");
    g_paste_settings_set_max_history_size (settings, 0);
    g_test_assert_expected_messages ();
    while (g_main_context_iteration (NULL, FALSE));

    g_autoptr (GPasteSettings) fresh = g_paste_settings_new ();

    g_assert_cmpuint (g_paste_settings_get_max_history_size (settings), ==, 250);
    g_assert_cmpuint (g_paste_settings_get_max_history_size (fresh), ==, 250);
    g_assert_cmpuint (notifications, ==, 0);

    /* Through the property, the setting is announced again once the write is
     * over, for a binding to read back what it holds. */
    g_test_expect_message ("GLib-GIO", G_LOG_LEVEL_WARNING, "*max-history-size*outside of valid range*");
    g_object_set (settings, G_PASTE_MAX_HISTORY_SIZE_SETTING, (guint64) 0, NULL);
    g_test_assert_expected_messages ();
    g_assert_cmpuint (notifications, ==, 0);
    while (g_main_context_iteration (NULL, FALSE));
    g_assert_cmpuint (notifications, ==, 1);
    g_assert_cmpuint (g_paste_settings_get_max_history_size (settings), ==, 250);

    g_paste_settings_reset (settings, G_PASTE_MAX_HISTORY_SIZE_SETTING);
}

int
main (int argc, char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    /* Isolate the optional keyfile backend as well as using memory GSettings. */
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func ("/settings/rebind-key", rebind_key);
    g_test_add_func ("/settings/migration-gate", migration_gate);
    g_test_add_func ("/settings/rejected-write", rejected_write);
    return g_paste_test_env_run ();
}
