// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>
#include <gpaste-test-env.h>
#include <gpaste-test-gtk.h>
#include <gpaste-3/gpaste-client.h>

typedef struct
{
    GAsyncReadyCallback callback;
    gpointer            data;
    GCancellable       *cancellable;
} PendingRead;

static PendingRead pending;
static gboolean have_display;

static void
read_item (GPasteClient       *client G_GNUC_UNUSED,
           const gchar        *uuid G_GNUC_UNUSED,
           GCancellable       *cancellable,
           GAsyncReadyCallback callback,
           gpointer            data)
{
    g_assert_nonnull (cancellable);
    pending = (PendingRead) { callback, data, g_object_ref (cancellable) };
}

static GPasteClientItem *
finish_item (GPasteClient *client G_GNUC_UNUSED,
             GAsyncResult *result G_GNUC_UNUSED,
             GError      **error G_GNUC_UNUSED)
{
    return g_paste_client_item_new ("00000000-0000-4000-8000-000000000001", "Text", G_PASTE_ITEM_KIND_TEXT, FALSE, NULL);
}

static GPasteTestGtkSave save;

static gchar *
finish_save (GPasteClient *client G_GNUC_UNUSED,
             GAsyncResult *result G_GNUC_UNUSED,
             GError      **error)
{
    return g_paste_test_gtk_save_finish (&save, error);
}

static void
send_replace (GPasteClient       *client G_GNUC_UNUSED,
              const gchar        *uuid G_GNUC_UNUSED,
              const gchar        *contents,
              GCancellable       *cancellable G_GNUC_UNUSED,
              GAsyncReadyCallback callback,
              gpointer            data)
{
    g_set_str (&save.text, contents);
    g_paste_test_gtk_save_send (&save, callback, data);
}

#define g_paste_client_get_item read_item
#define g_paste_client_get_item_finish finish_item
#define g_paste_client_replace send_replace
#define g_paste_client_replace_finish finish_save
#include "../../src/ui/gpaste-ui-edit-item.c"
#undef g_paste_client_replace_finish
#undef g_paste_client_replace
#undef g_paste_client_get_item_finish
#undef g_paste_client_get_item

static void
reply (GPasteClient *client)
{
    PendingRead read = pending;

    pending = (PendingRead) { 0 };
    read.callback (G_OBJECT (client), NULL, read.data);
    g_object_unref (read.cancellable);
}

/* The edit's read goes out on a cancellable its window cancels when it goes, and
 * holds that window weakly: a closed window gets no dialog, whether GTK let go of
 * it at once or another operation still holds it, and the edit is not what keeps
 * it alive. A window still up gets its dialog, and neither does the dialog keep
 * the window alive once the user closes it. */
static void
edit_window_lifetime (gconstpointer user_data)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    guint variant = GPOINTER_TO_UINT (user_data);
    g_autoptr (GPasteClient) client = g_object_new (G_PASTE_TYPE_CLIENT, NULL);
    GtkWindow *window = GTK_WINDOW (adw_window_new ());
    gpointer weak_window = window;

    g_object_add_weak_pointer (G_OBJECT (window), &weak_window);
    gtk_window_present (window);
    g_paste_ui_edit_item_show (client, window, "00000000-0000-4000-8000-000000000001");
    g_assert_nonnull (pending.callback);
    g_autoptr (GCancellable) cancellable = g_object_ref (pending.cancellable);

    if (variant == 0)
    {
        /* No test-owned window ref: GTK releases its ownership on destroy, and
         * an edit retaining the parent would keep it up. */
        gtk_window_destroy (window);
        g_assert_null (weak_window);
        g_assert_true (g_cancellable_is_cancelled (cancellable));
        reply (client);
    }
    else if (variant == 1)
    {
        g_autoptr (GtkWindow) retained = g_object_ref (window);

        gtk_window_destroy (window);
        g_assert_true (g_cancellable_is_cancelled (cancellable));
        reply (client);
        g_assert_null (adw_window_get_visible_dialog (ADW_WINDOW (window)));
        g_clear_object (&retained);
        g_assert_null (weak_window);
    }
    else
    {
        reply (client);
        g_assert_false (g_cancellable_is_cancelled (cancellable));

        AdwDialog *dialog = adw_window_get_visible_dialog (ADW_WINDOW (window));

        g_assert_nonnull (dialog);
        /* Closed under the dialog, the window still goes: the dialog's state
         * does not hold it either. And that state is freed with it, the dialog
         * answering as it goes -- the client reference it holds included --
         * even while something else still holds the dialog itself. */
        if (variant == 3)
        {
            g_autoptr (AdwDialog) held = g_object_ref (dialog);

            gtk_window_destroy (window);
            g_assert_null (weak_window);
            g_assert_cmpuint (G_OBJECT (client)->ref_count, ==, 1);
            return;
        }
        adw_dialog_force_close (dialog);
        gtk_window_destroy (window);
        g_assert_null (weak_window);
    }
}

static void
never_submitted (AdwDialog   *dialog G_GNUC_UNUSED,
                 const gchar *text G_GNUC_UNUSED,
                 gpointer     user_data G_GNUC_UNUSED)
{
    g_assert_not_reached ();
}

static void
count_release (gpointer user_data)
{
    ++*(guint *) user_data;
}

/* A text dialog is only put up over a window it can live in: libadwaita gives it
 * a window of its own over anything else, with a lifecycle of its own that
 * g_paste_gtk_util_text_dialog () refuses to take on. Such a parent is a
 * programmer error, reported as one, and nothing is put up -- but @destroy is
 * still called, once, the caller's state being freed there. With @user_data,
 * the parent is an Adwaita window that cannot be resized, which libadwaita does
 * not host a dialog in either. */
static void
foreign_parent_refused (gconstpointer user_data)
{
    gboolean adwaita_fixed_size = GPOINTER_TO_INT (user_data);

    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    if (g_test_subprocess ())
    {
        GtkWindow *parent = GTK_WINDOW ((adwaita_fixed_size) ? adw_window_new () : gtk_window_new ());
        guint answers = 0;

        gtk_window_set_resizable (parent, !adwaita_fixed_size);
        /* The refusal is a critical, which g_test_init () makes fatal: survived
         * here, so that what comes after it can be checked. */
        g_log_set_always_fatal (G_LOG_FATAL_MASK);
        g_paste_gtk_util_text_dialog (parent, "Edit Item", "Save", "some text", never_submitted, &answers, count_release);
        g_assert_cmpuint (answers, ==, 1);

        /* Nothing was connected to the parent either. */
        gtk_window_destroy (parent);
        g_paste_test_bus_drain ();
        g_assert_cmpuint (answers, ==, 1);
        return;
    }

    g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
    g_test_trap_assert_passed ();
    g_test_trap_assert_stderr ("*CRITICAL*g_paste_gtk_util_text_dialog: assertion failed*");
}

/* A window the dialog would refuse is refused before the read goes out, where
 * the caller's mistake is made, rather than after a round trip to the daemon. */
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

        /* As in foreign_parent_refused (). */
        g_log_set_always_fatal (G_LOG_FATAL_MASK);
        g_paste_ui_edit_item_show (client, window, "00000000-0000-4000-8000-000000000001");
        g_assert_null (pending.callback);
        g_assert_cmpuint (G_OBJECT (client)->ref_count, ==, 1);

        gtk_window_destroy (window);
        return;
    }

    g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
    g_test_trap_assert_passed ();
    g_test_trap_assert_stderr ("*CRITICAL*g_paste_ui_edit_item_show*g_paste_gtk_util_can_host_dialog*");
}

/* The edit dialog over @window, its read answered. */
static AdwDialog *
edit_dialog (GPasteClient *client,
             GtkWindow    *window)
{
    g_paste_ui_edit_item_show (client, window, "00000000-0000-4000-8000-000000000001");
    reply (client);

    AdwDialog *dialog = adw_window_get_visible_dialog (ADW_WINDOW (window));

    g_assert_nonnull (dialog);

    return dialog;
}

/* Saving the text as it was read sends nothing and closes: Replace would make
 * a new item of it all the same, and drop its rich text. */
static void
unchanged_not_saved (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GPasteClient) client = g_object_new (G_PASTE_TYPE_CLIENT, NULL);
    GtkWindow *window = GTK_WINDOW (adw_window_new ());

    gtk_window_present (window);
    save.sent = 0;

    AdwDialog *dialog = edit_dialog (client, window);

    g_signal_emit_by_name (g_paste_test_gtk_find_button (GTK_WIDGET (dialog), "Save"), "clicked");
    g_paste_test_gtk_wait_dialog_closed (window);
    g_assert_cmpuint (save.sent, ==, 0);
    gtk_window_destroy (window);
}

/* An edit the daemon refuses -- the item gone meanwhile, or too big -- is not
 * lost with the dialog. */
static void
refusal_kept (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GPasteClient) client = g_object_new (G_PASTE_TYPE_CLIENT, NULL);
    GtkWindow *window = GTK_WINDOW (adw_window_new ());

    gtk_window_present (window);

    AdwDialog *dialog = edit_dialog (client, window);

    g_paste_test_gtk_assert_refusal_kept (&save, client, window, dialog, "Save", "Text edited", "Could not save the edited item");
    gtk_window_destroy (window);
}

/* A window going while the save is out takes the dialog, and frees what the
 * edit held; the answer after that has nothing left to tell -- @user_data
 * holding the dialog alive across it, as an accessible or an animation can,
 * the answer reaching a dialog no longer presented, which it leaves alone. */
static void
window_gone_while_saving (gconstpointer user_data)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GPasteClient) client = g_object_new (G_PASTE_TYPE_CLIENT, NULL);
    GtkWindow *window = GTK_WINDOW (adw_window_new ());

    gtk_window_present (window);

    AdwDialog *dialog = edit_dialog (client, window);
    g_autoptr (AdwDialog) held = (user_data) ? g_object_ref (dialog) : NULL;

    g_paste_test_gtk_set_dialog_text (dialog, "Text edited");
    g_signal_emit_by_name (g_paste_test_gtk_find_button (GTK_WIDGET (dialog), "Save"), "clicked");
    gtk_window_destroy (window);
    g_assert_cmpuint (G_OBJECT (client)->ref_count, ==, 1);
    g_paste_test_gtk_save_answer (&save, client);
}

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DISPLAY);
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    have_display = g_paste_test_env_has_display () && gtk_init_check ();
    if (have_display)
        adw_init ();
    g_test_add_data_func ("/ui/edit_item/closed_parent", GUINT_TO_POINTER (0), edit_window_lifetime);
    g_test_add_data_func ("/ui/edit_item/retained_closed_parent", GUINT_TO_POINTER (1), edit_window_lifetime);
    g_test_add_data_func ("/ui/edit_item/open_parent", GUINT_TO_POINTER (2), edit_window_lifetime);
    g_test_add_data_func ("/ui/edit_item/closed_with_dialog_up", GUINT_TO_POINTER (3), edit_window_lifetime);
    g_test_add_data_func ("/ui/text_dialog_answer/foreign_parent_refused", GINT_TO_POINTER (FALSE), foreign_parent_refused);
    g_test_add_data_func ("/ui/text_dialog_answer/fixed_size_parent_refused", GINT_TO_POINTER (TRUE), foreign_parent_refused);
    g_test_add_func ("/ui/edit_item/unhostable_window_refused", unhostable_window_refused);
    g_test_add_func ("/ui/edit_item/unchanged_not_saved", unchanged_not_saved);
    g_test_add_func ("/ui/edit_item/refusal_kept", refusal_kept);
    g_test_add_data_func ("/ui/edit_item/window_gone_while_saving", GINT_TO_POINTER (FALSE), window_gone_while_saving);
    g_test_add_data_func ("/ui/edit_item/window_gone_while_saving_held", GINT_TO_POINTER (TRUE), window_gone_while_saving);
    return g_paste_test_env_run ();
}
