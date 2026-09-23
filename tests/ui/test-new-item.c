// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-env.h>
#include <gpaste-3/gpaste-client.h>

static gboolean have_display;

#include "../../src/ui/gpaste-ui-new-item.c"

/* Only the composer's lifetime is under test: adding what was written is the
 * window's business, and linking it in would bring the whole graphical tool
 * along. */
gpointer
g_paste_ui_report_string (GtkWidget           *origin G_GNUC_UNUSED,
                          GPasteUiStringFinish finish G_GNUC_UNUSED,
                          const gchar         *message G_GNUC_UNUSED)
{
    g_assert_not_reached ();
}

void
g_paste_ui_report_string_cb (GObject      *source_object G_GNUC_UNUSED,
                             GAsyncResult *res G_GNUC_UNUSED,
                             gpointer      user_data G_GNUC_UNUSED)
{
    g_assert_not_reached ();
}

/* The composer holds its window weakly and is answered when the window goes
 * under it, so closing the window with the dialog up lets the window go and
 * frees what the composer held, the client reference included. */
static void
closed_with_dialog_up (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GPasteClient) client = g_object_new (G_PASTE_TYPE_CLIENT, NULL);
    GtkWindow *window = GTK_WINDOW (adw_window_new ());
    gpointer weak_window = window;

    g_object_add_weak_pointer (G_OBJECT (window), &weak_window);
    gtk_window_present (window);
    g_paste_ui_new_item_show (client, window);
    g_assert_nonnull (adw_window_get_visible_dialog (ADW_WINDOW (window)));

    gtk_window_destroy (window);
    g_assert_null (weak_window);
    g_assert_cmpuint (G_OBJECT (client)->ref_count, ==, 1);
}

/* A window the composer's dialog would refuse is refused on the way in, and
 * nothing is held for it. */
static void
unhostable_window_refused (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    if (g_test_subprocess ())
    {
        g_autoptr (GPasteClient) client = g_object_new (G_PASTE_TYPE_CLIENT, NULL);
        GtkWindow *window = GTK_WINDOW (gtk_window_new ());

        /* As in test-edit-item.c's foreign_parent_refused (). */
        g_log_set_always_fatal (G_LOG_FATAL_MASK);
        g_paste_ui_new_item_show (client, window);
        g_assert_cmpuint (G_OBJECT (client)->ref_count, ==, 1);

        gtk_window_destroy (window);
        return;
    }

    g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
    g_test_trap_assert_passed ();
    g_test_trap_assert_stderr ("*CRITICAL*g_paste_ui_new_item_show*g_paste_gtk_util_can_host_dialog*");
}

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DISPLAY);
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    have_display = g_paste_test_env_has_display () && gtk_init_check ();
    if (have_display)
        adw_init ();
    g_test_add_func ("/ui/new_item/closed_with_dialog_up", closed_with_dialog_up);
    g_test_add_func ("/ui/new_item/unhostable_window_refused", unhostable_window_refused);
    return g_paste_test_env_run ();
}
