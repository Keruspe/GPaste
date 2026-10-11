// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>
#include <gpaste-test-env.h>
#include <gpaste-3/gpaste-settings.h>
#include <gpaste-3/gpaste-util.h>
#include <gpaste-3/gpaste-error.h>
#include <gpaste-3/gpaste-gsettings-keys.h>

#include <glib/gstdio.h>

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

/* A settings file under @directory of the configuration directory, holding
 * @contents, as a user opting into the keyfile backend makes one. */
static gchar *
keyfile_new (const gchar *directory,
             const gchar *contents)
{
    g_autofree gchar *dir = g_build_filename (g_get_user_config_dir (), directory, NULL);
    gchar *path = g_build_filename (dir, "settings", NULL);

    g_assert_cmpint (g_mkdir_with_parents (dir, 0700), ==, 0);
    g_assert_true (g_file_set_contents (path, contents, -1, NULL));

    return path;
}

static gchar *
keyfile_value (const gchar *path,
               const gchar *key)
{
    g_autoptr (GKeyFile) keyfile = g_key_file_new ();

    g_assert_true (g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, NULL));

    return g_key_file_get_value (keyfile, PACKAGE_NAME, key, NULL);
}

/* Two settings instances in one process share the file's backend: what one
 * writes is not undone by the other writing the whole file from a copy of its
 * own taken before. */
static void
keyfile_shared (void)
{
    g_autofree gchar *path = keyfile_new (PACKAGE, "");
    g_autoptr (GPasteSettings) first = g_paste_settings_new ();
    g_autoptr (GPasteSettings) second = g_paste_settings_new ();

    g_paste_settings_set_max_history_size (first, 100);
    g_paste_settings_set_track_changes (second, FALSE);

    g_autofree gchar *size = keyfile_value (path, G_PASTE_MAX_HISTORY_SIZE_SETTING);
    g_autofree gchar *track = keyfile_value (path, G_PASTE_TRACK_CHANGES_SETTING);

    g_assert_cmpstr (size, ==, "100");
    g_assert_cmpstr (track, ==, "false");
}

/* A reload reads what the file holds now, another process having written it,
 * without waiting for the backend's file monitor. */
static void
keyfile_reload (void)
{
    g_autofree gchar *path = keyfile_new (PACKAGE, "");
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_assert_true (g_file_set_contents (path, "[" PACKAGE_NAME "]\nmax-history-size=77\n", -1, NULL));
    g_paste_settings_reload (settings);
    g_assert_cmpuint (g_paste_settings_get_max_history_size (settings), ==, 77);
}

/* A reload after the file is removed keeps what the process was using -- what
 * its backend still holds and writes to -- rather than reading every key as
 * its default. */
static void
keyfile_removed (void)
{
    g_autofree gchar *path = keyfile_new (PACKAGE, "[" PACKAGE_NAME "]\nmax-history-size=77\nhistory-name='work'\n");
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_assert_cmpuint (g_paste_settings_get_max_history_size (settings), ==, 77);
    g_assert_cmpint (g_unlink (path), ==, 0);
    g_paste_settings_reload (settings);
    g_assert_cmpuint (g_paste_settings_get_max_history_size (settings), ==, 77);
    g_assert_cmpstr (g_paste_settings_get_history_name (settings), ==, "work");
}

/* A gate the settings file could not be rewritten to open is none: the reset
 * the keyfile backend takes in memory is not what the file, which the daemon
 * reads, still says. */
static void
keyfile_gate_unwritable (void)
{
    if (!g_test_subprocess ())
    {
        if (!geteuid ())
        {
            g_test_skip ("root writes a directory of mode 0500");
            return;
        }

        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*GLib-GIO-WARNING*Failed to write keyfile*");
        g_test_trap_assert_stderr_unmatched ("*WARNING*WARNING*");
        g_test_trap_assert_stderr_unmatched ("*CRITICAL*");
        return;
    }

    /* Writing the file fails with a warning of GLib's, which is the case, and
     * the parent checks it is the only thing logged. */
    g_log_set_always_fatal (G_LOG_LEVEL_ERROR);

    g_autofree gchar *path = keyfile_new (PACKAGE, "[" PACKAGE_NAME "]\nstorage-backend-revision=37\n");
    g_autofree gchar *dir = g_path_get_dirname (path);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GError) error = NULL;
    guint64 revision = 0;

    g_assert_cmpuint (g_paste_settings_get_storage_backend_revision (settings), ==, 37);
    g_assert_cmpint (g_chmod (dir, 0500), ==, 0);

    gboolean opened = g_paste_util_open_storage_migration (&revision, &error);

    g_assert_cmpint (g_chmod (dir, 0700), ==, 0);
    g_assert_false (opened);
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
}

/* A settings file where the meson builds defining PACKAGE as "GPaste" looked
 * for it is still found. */
static void
keyfile_legacy_path (void)
{
    g_autofree gchar *path = keyfile_new (PACKAGE_NAME, "[" PACKAGE_NAME "]\nmax-history-size=88\n");
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_assert_cmpuint (g_paste_settings_get_max_history_size (settings), ==, 88);
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
    g_test_add_func ("/settings/keyfile/shared", keyfile_shared);
    g_test_add_func ("/settings/keyfile/reload", keyfile_reload);
    g_test_add_func ("/settings/keyfile/removed", keyfile_removed);
    g_test_add_func ("/settings/keyfile/gate-unwritable", keyfile_gate_unwritable);
    g_test_add_func ("/settings/keyfile/legacy-path", keyfile_legacy_path);
    return g_paste_test_env_run ();
}
