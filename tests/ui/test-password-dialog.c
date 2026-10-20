// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-env.h>
#include <gpaste-3/gpaste-client.h>

typedef struct
{
    GAsyncReadyCallback callback;
    gpointer            data;
    GCancellable       *cancellable;
} PendingRead;

static PendingRead reads[2];
static gboolean have_display;

static void
defer_read (guint               index,
            GCancellable       *cancellable,
            GAsyncReadyCallback callback,
            gpointer            data)
{
    reads[index] = (PendingRead) { callback, data, g_object_ref (cancellable) };
}

static void
read_item (GPasteClient       *client G_GNUC_UNUSED,
           const gchar        *uuid G_GNUC_UNUSED,
           GCancellable       *cancellable,
           GAsyncReadyCallback callback,
           gpointer            data)
{
    defer_read (0, cancellable, callback, data);
}

static void
read_timeout (GPasteClient       *client G_GNUC_UNUSED,
              const gchar        *uuid G_GNUC_UNUSED,
              GCancellable       *cancellable,
              GAsyncReadyCallback callback,
              gpointer            data)
{
    defer_read (1, cancellable, callback, data);
}

static GPasteClientItem *
finish_item (GPasteClient *client G_GNUC_UNUSED,
             GAsyncResult *result G_GNUC_UNUSED,
             GError      **error G_GNUC_UNUSED)
{
    return g_paste_client_item_new ("00000000-0000-4000-8000-000000000001", "Test", G_PASTE_ITEM_KIND_PASSWORD, FALSE, NULL);
}

static guint
finish_timeout (GPasteClient *client G_GNUC_UNUSED,
                GAsyncResult *result G_GNUC_UNUSED,
                GError      **error G_GNUC_UNUSED)
{
    return 42;
}

static void make_password (GPasteClient       *client,
                           const gchar        *uuid,
                           const gchar        *name,
                           guint               timeout,
                           GCancellable       *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer            data);

/* Keep GTK's real window/dialog lifetime while controlling both reply orders. */
#define g_paste_client_get_item read_item
#define g_paste_client_get_password_timeout read_timeout
#define g_paste_client_get_item_finish finish_item
#define g_paste_client_get_password_timeout_finish finish_timeout
#define g_paste_client_make_password make_password
#include "../../src/ui/gpaste-ui-password-dialog.c"
#undef g_paste_client_make_password
#undef g_paste_client_get_password_timeout_finish
#undef g_paste_client_get_item_finish
#undef g_paste_client_get_password_timeout
#undef g_paste_client_get_item

/* The timeout a confirmed edit sends, G_MAXUINT before one is sent. */
static guint made_timeout = G_MAXUINT;

static void
make_password (GPasteClient       *client G_GNUC_UNUSED,
               const gchar        *uuid G_GNUC_UNUSED,
               const gchar        *name G_GNUC_UNUSED,
               guint               timeout,
               GCancellable       *cancellable G_GNUC_UNUSED,
               GAsyncReadyCallback callback G_GNUC_UNUSED,
               gpointer            data)
{
    PasswordRequest *request = data;

    made_timeout = timeout;
    g_weak_ref_clear (&request->dialog);
    g_free (request);
}

static void
reply (GPasteClient *client,
       guint         index)
{
    PendingRead read = reads[index];

    reads[index] = (PendingRead) { 0 };
    read.callback (G_OBJECT (client), NULL, read.data);
    g_object_unref (read.cancellable);
}

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
    g_paste_ui_password_dialog_edit (client, window, "00000000-0000-4000-8000-000000000001");
    g_assert_true (reads[0].cancellable == reads[1].cancellable);
    g_autoptr (GCancellable) cancellable = g_object_ref (reads[0].cancellable);

    if (variant < 2)
    {
        /* No test-owned window ref: GTK releases its ownership on destroy.
         * An edit retaining the parent would keep
         * both the window and reads up. */
        gtk_window_destroy (window);
        g_assert_null (weak_window);
        g_assert_true (g_cancellable_is_cancelled (cancellable));
        reply (client, variant);
        reply (client, 1 - variant);
    }
    else if (variant == 2)
    {
        reply (client, 0);
        gtk_window_destroy (window);
        g_assert_null (weak_window);
        g_assert_true (g_cancellable_is_cancelled (cancellable));
        reply (client, 1);
    }
    else if (variant == 3)
    {
        /* Hiding a live window is not closing it;
         * the edit still belongs to it. */
        gtk_widget_set_visible (GTK_WIDGET (window), FALSE);
        g_assert_false (g_cancellable_is_cancelled (cancellable));
        reply (client, 1);
        reply (client, 0);
        g_assert_nonnull (adw_window_get_visible_dialog (ADW_WINDOW (window)));
        gtk_window_destroy (window);
        g_assert_null (weak_window);
    }
    else
    {
        /* Another operation may keep the closed window addressable. */
        g_autoptr (GtkWindow) retained = g_object_ref (window);

        gtk_window_destroy (window);
        g_assert_nonnull (weak_window);
        g_assert_true (g_cancellable_is_cancelled (cancellable));
        reply (client, 0);
        reply (client, 1);
        g_assert_null (adw_window_get_visible_dialog (ADW_WINDOW (window)));
        g_clear_object (&retained);
        g_assert_null (weak_window);
    }
}

static AdwSpinRow *
find_spin_row (GtkWidget *widget)
{
    if (ADW_IS_SPIN_ROW (widget))
        return ADW_SPIN_ROW (widget);

    for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    {
        AdwSpinRow *row = find_spin_row (child);

        if (row)
            return row;
    }

    return NULL;
}

/* Whether every row under @widget shows its title as written. */
static gboolean
rows_plain (GtkWidget *widget)
{
    if (ADW_IS_PREFERENCES_ROW (widget) && adw_preferences_row_get_use_markup (ADW_PREFERENCES_ROW (widget)))
        return FALSE;

    for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    {
        if (!rows_plain (child))
            return FALSE;
    }

    return TRUE;
}

/* A timeout field cleared to type another number into, or holding none, keeps
 * the timeout the password had rather than sending 0, which would never take it
 * off the clipboard. */
static void
timeout_keeps_blank (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GPasteClient) client = g_object_new (G_PASTE_TYPE_CLIENT, NULL);
    GtkWindow *window = GTK_WINDOW (adw_window_new ());

    gtk_window_present (window);
    g_paste_ui_password_dialog_edit (client, window, "00000000-0000-4000-8000-000000000001");
    reply (client, 0);
    reply (client, 1);

    AdwDialog *dialog = adw_window_get_visible_dialog (ADW_WINDOW (window));
    PasswordDialogData *data = g_object_get_data (G_OBJECT (dialog), "password-dialog-data");
    AdwSpinRow *timeout = find_spin_row (GTK_WIDGET (dialog));

    g_assert_nonnull (timeout);
    /* Its titles being translations, taken as markup they would empty. */
    g_assert_true (rows_plain (GTK_WIDGET (dialog)));
    for (guint i = 0; i < 2; ++i)
    {
        gtk_editable_set_text (GTK_EDITABLE (timeout), (i) ? "abc" : "");
        adw_spin_row_update (timeout);
        made_timeout = G_MAXUINT;
        data->submitted = FALSE;
        g_signal_emit_by_name (data->confirm, "clicked");
        g_assert_cmpuint (made_timeout, ==, 42);
    }

    gtk_window_destroy (window);
}

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DISPLAY);
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    have_display = g_paste_test_env_has_display () && gtk_init_check ();
    if (have_display)
        adw_init ();
    g_test_add_data_func ("/ui/password/close_item_first", GUINT_TO_POINTER (0), edit_window_lifetime);
    g_test_add_data_func ("/ui/password/close_timeout_first", GUINT_TO_POINTER (1), edit_window_lifetime);
    g_test_add_data_func ("/ui/password/close_between_replies", GUINT_TO_POINTER (2), edit_window_lifetime);
    g_test_add_data_func ("/ui/password/hidden_parent", GUINT_TO_POINTER (3), edit_window_lifetime);
    g_test_add_data_func ("/ui/password/retained_closed_parent", GUINT_TO_POINTER (4), edit_window_lifetime);
    g_test_add_func ("/ui/password/timeout_keeps_blank", timeout_keeps_blank);
    return g_paste_test_env_run ();
}
