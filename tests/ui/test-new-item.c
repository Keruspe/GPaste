// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-env.h>
#include <gpaste-test-gtk.h>
#include <gpaste-3/gpaste-client.h>

static gboolean have_display;

static GPasteTestGtkSave save;

static gchar *
finish_save (GPasteClient *client G_GNUC_UNUSED,
             GAsyncResult *result G_GNUC_UNUSED,
             GError      **error)
{
    return g_paste_test_gtk_save_finish (&save, error);
}

static void
send_add_text (GPasteClient       *client G_GNUC_UNUSED,
               const gchar        *text,
               GCancellable       *cancellable G_GNUC_UNUSED,
               GAsyncReadyCallback callback,
               gpointer            data)
{
    g_assert_cmpstr (text, ==, "Text");
    g_paste_test_gtk_save_send (&save, callback, data);
}

#define g_paste_client_add_text send_add_text
#define g_paste_client_add_text_finish finish_save
#include "../../src/ui/gpaste-ui-new-item.c"
#undef g_paste_client_add_text_finish
#undef g_paste_client_add_text

/* A text the daemon refuses to add is not lost with the dialog, over an
 * application window as the graphical tool's is. */
static void
refusal_kept (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GPasteClient) client = g_object_new (G_PASTE_TYPE_CLIENT, NULL);
    GtkWindow *window = GTK_WINDOW (adw_application_window_new (NULL));

    gtk_window_present (window);
    g_paste_ui_new_item_show (client, window);

    AdwDialog *dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (window));

    g_paste_test_gtk_set_dialog_text (dialog, "Text");
    g_signal_emit_by_name (g_paste_test_gtk_find_button (GTK_WIDGET (dialog), "Add"), "clicked");
    g_assert_cmpuint (save.sent, ==, 1);
    {
        g_autofree gchar *text = g_paste_test_gtk_dialog_text (dialog);

        g_assert_cmpstr (text, ==, "Text");
    }
    g_paste_test_gtk_assert_refusal_kept (&save, client, window, dialog, "Add", "Could not add the item");
    g_assert_cmpuint (save.sent, ==, 2);
    gtk_window_destroy (window);
}

/* The composer holds no reference to its window and lets go of what it holds
 * when the window goes under it, so closing the window with the dialog up lets
 * the window go and frees the client reference. */
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
    g_test_add_func ("/ui/new_item/refusal_kept", refusal_kept);
    return g_paste_test_env_run ();
}
