// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* Access the dialog lifecycle without starting the asynchronous daemon client. */
#include <gpaste-ui-window.c>

static gboolean
elapsed (gpointer user_data)
{
    *(gboolean *) user_data = TRUE;
    return G_SOURCE_REMOVE;
}

static void
wait_for_close (void)
{
    gboolean done = FALSE;
    g_timeout_add (350, elapsed, &done);
    while (!done)
        g_main_context_iteration (NULL, TRUE);
}

static gboolean have_display;

static void
shortcuts_changed (gconstpointer user_data)
{
    if (!have_display)
    {
        g_test_skip ("A GTK display is required");
        return;
    }

    g_autoptr (GPasteUiWindow) window = g_object_ref_sink (g_object_new (G_PASTE_TYPE_UI_WINDOW, NULL));
    g_paste_settings_set_keybindings_enabled (window->settings, TRUE);
    gtk_window_present (GTK_WINDOW (window));
    on_show_help_overlay (NULL, NULL, window);
    g_assert_nonnull (window->shortcuts);
    g_autoptr (AdwDialog) original = g_object_ref (window->shortcuts);

    if (GPOINTER_TO_INT (user_data))
        g_paste_settings_set_keybindings_enabled (window->settings, FALSE);
    else
        g_paste_settings_set_launch_ui (window->settings, "<Control>F12");

    g_assert_cmpuint (window->shortcuts_source, !=, 0);
    wait_for_close ();
    g_assert_null (window->shortcuts);
    g_assert_cmpuint (window->shortcuts_source, ==, 0);

    on_show_help_overlay (NULL, NULL, window);
    g_assert_nonnull (window->shortcuts);
    g_assert_true (window->shortcuts != original);
    g_paste_settings_set_keybindings_enabled (window->settings, TRUE);
    if (GPOINTER_TO_INT (user_data))
    {
        g_assert_cmpuint (window->shortcuts_source, !=, 0);
        wait_for_close ();
        g_assert_null (window->shortcuts);
    }
    gtk_window_destroy (GTK_WINDOW (window));
}

int
main (int argc, char **argv)
{
    /* Display sockets live in the runtime directory that isolation replaces. */
    have_display = gtk_init_check ();
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    if (have_display)
        adw_init ();
    g_test_add_data_func ("/ui/shortcuts/master-switch", GINT_TO_POINTER (TRUE), shortcuts_changed);
    g_test_add_data_func ("/ui/shortcuts/accelerator", GINT_TO_POINTER (FALSE), shortcuts_changed);
    return g_test_run ();
}
