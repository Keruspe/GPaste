// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-settings.h>

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

int
main (int argc, char **argv)
{
    /* Isolate the optional keyfile backend as well as using memory GSettings. */
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func ("/settings/rebind-key", rebind_key);
    return g_test_run ();
}
