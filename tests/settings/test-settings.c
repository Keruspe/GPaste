// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-settings.h>

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
on_changed (GPasteSettings *settings G_GNUC_UNUSED,
            const gchar    *key G_GNUC_UNUSED,
            gpointer        user_data)
{
    ++*((guint *) user_data);
}

static gboolean
listed (GSettings   *shell,
        const gchar *list)
{
    g_auto (GStrv) extensions = g_settings_get_strv (shell, list);

    return g_strv_contains ((const gchar * const *) extensions, G_PASTE_EXTENSION_NAME);
}

/* The extension is enabled as GNOME Shell decides it: listed in
 * enabled-extensions, user extensions not all turned off, and not listed in
 * disabled-extensions, which wins; enabling and disabling it write both
 * lists, as the shell does. */
static void
extension_enabled (void)
{
    g_autoptr (GSettings) shell = g_settings_new ("org.gnome.shell");
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar * const ours[] = { G_PASTE_EXTENSION_NAME, NULL };
    const gchar * const none[] = { NULL };
    guint changes = 0;

    g_signal_connect (settings, "changed::extension-enabled", G_CALLBACK (on_changed), &changes);

    g_settings_set_strv (shell, "enabled-extensions", ours);
    g_assert_true (g_paste_settings_get_extension_enabled (settings));

    g_settings_set_strv (shell, "disabled-extensions", ours);
    g_assert_false (g_paste_settings_get_extension_enabled (settings));

    g_settings_set_strv (shell, "disabled-extensions", none);
    g_assert_true (g_paste_settings_get_extension_enabled (settings));

    g_settings_set_boolean (shell, "disable-user-extensions", TRUE);
    g_assert_false (g_paste_settings_get_extension_enabled (settings));

    /* Enabled while the shell runs no user extension: the lists already say
     * so, nothing is written, and the state is announced all the same for a
     * switch to read back. */
    g_paste_settings_set_extension_enabled (settings, TRUE);
    g_assert_false (g_paste_settings_get_extension_enabled (settings));
    g_assert_cmpuint (changes, ==, 5);
    g_assert_true (listed (shell, "enabled-extensions"));
    g_assert_false (listed (shell, "disabled-extensions"));

    /* A list written while user extensions are off is announced once, by
     * the write. */
    g_settings_set_strv (shell, "enabled-extensions", none);
    g_assert_cmpuint (changes, ==, 6);
    g_paste_settings_set_extension_enabled (settings, TRUE);
    g_assert_false (g_paste_settings_get_extension_enabled (settings));
    g_assert_cmpuint (changes, ==, 7);

    g_settings_set_boolean (shell, "disable-user-extensions", FALSE);
    g_assert_cmpuint (changes, ==, 8);

    changes = 0;
    g_paste_settings_set_extension_enabled (settings, FALSE);
    g_assert_false (g_paste_settings_get_extension_enabled (settings));
    g_assert_false (listed (shell, "enabled-extensions"));
    g_assert_true (listed (shell, "disabled-extensions"));

    g_paste_settings_set_extension_enabled (settings, TRUE);
    g_assert_true (g_paste_settings_get_extension_enabled (settings));
    g_assert_true (listed (shell, "enabled-extensions"));
    g_assert_false (listed (shell, "disabled-extensions"));

    /* Each list written once each way. */
    g_assert_cmpuint (changes, ==, 4);

    g_settings_set_strv (shell, "disabled-extensions", none);
    g_paste_settings_set_extension_enabled (settings, FALSE);
    g_paste_settings_set_extension_enabled (settings, TRUE);
    g_assert_cmpuint (changes, ==, 8);
}

/* Writable again, for the isolated directories to be removed. */
static void
release_read_only (gchar *dir)
{
    g_assert_no_errno (g_chmod (dir, 0700));
    g_free (dir);
}

/* A list the user cannot write leaves the extension as it is, announced all
 * the same for a switch to read back: a keyfile in a directory gone read-only,
 * as a dconf lock would, refuses the write. */
static void
extension_enabled_unwritable (void)
{
    if (!g_test_subprocess ())
    {
        g_auto (GStrv) envp = g_environ_setenv (g_get_environ (), "GSETTINGS_BACKEND", "keyfile", TRUE);
        g_autofree gchar *probe = g_build_filename (g_get_user_config_dir (), "read-only", NULL);

        /* Skipped here rather than in the child, whose skip would pass for a
         * pass: whoever writes a read-only directory all the same, as root
         * does, has no list it cannot write. */
        g_assert_no_errno (g_mkdir_with_parents (g_get_user_config_dir (), 0700));
        g_assert_no_errno (g_mkdir (probe, 0500));

        gboolean writable = !g_access (probe, W_OK);

        g_assert_no_errno (g_rmdir (probe));
        if (writable)
        {
            g_test_skip ("a read-only directory is written all the same");
            return;
        }

        g_test_trap_subprocess_with_envp (NULL, (const gchar * const *) envp, 0, G_TEST_SUBPROCESS_DEFAULT);

        /* Writable again even when the child stopped short of doing so, for
         * the isolated directories to be removed: the child's are the case's
         * own (G_TEST_TMPDIR). */
        g_autofree gchar *dir = g_build_filename (g_get_user_config_dir (), "glib-2.0", "settings", NULL);

        g_chmod (dir, 0700);
        g_test_trap_assert_passed ();
        return;
    }

    gchar *dir = g_build_filename (g_get_user_config_dir (), "glib-2.0", "settings", NULL);
    g_autofree gchar *file = g_build_filename (dir, "keyfile", NULL);
    g_autofree gchar *contents = g_strdup_printf ("[org/gnome/shell]\nenabled-extensions=['%s']\n", G_PASTE_EXTENSION_NAME);

    g_assert_no_errno (g_mkdir_with_parents (dir, 0700));
    g_assert_true (g_file_set_contents (file, contents, -1, NULL));
    g_assert_no_errno (g_chmod (dir, 0500));
    g_test_queue_destroy ((GDestroyNotify) release_read_only, dir);

    g_autoptr (GSettings) shell = g_settings_new ("org.gnome.shell");

    g_assert_false (g_settings_is_writable (shell, "enabled-extensions"));

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    guint changes = 0;

    g_signal_connect (settings, "changed::extension-enabled", G_CALLBACK (on_changed), &changes);

    g_assert_true (g_paste_settings_get_extension_enabled (settings));
    g_paste_settings_set_extension_enabled (settings, FALSE);
    g_assert_true (g_paste_settings_get_extension_enabled (settings));
    g_assert_true (listed (shell, "enabled-extensions"));
    g_assert_cmpuint (changes, ==, 1);
}

int
main (int argc, char **argv)
{
    /* Isolate the optional keyfile backend as well as using memory GSettings. */
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func ("/settings/rebind-key", rebind_key);
    g_test_add_func ("/settings/extension-enabled", extension_enabled);
    g_test_add_func ("/settings/extension-enabled-unwritable", extension_enabled_unwritable);
    return g_test_run ();
}
