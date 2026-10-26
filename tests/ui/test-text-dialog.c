// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>
#include <gpaste-test-env.h>
#include <gpaste-test-gtk.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>
#include <gpaste-3/gpaste-gsettings-keys.h>

static gboolean have_display;

/* What the composer refuses to lay out, and what it is happy with: the cost is
 * the length of a line, so the long multi-line case has to stay allowed. */
#define OVER_THE_LIMIT  60000
#define UNDER_THE_LIMIT 40000

static gchar *
make_line (gsize length)
{
    gchar *line = g_malloc (length + 1);

    memset (line, 'a', length);
    line[length] = '\0';

    return line;
}

/* Many lines, each far under the limit, adding up to well over it, each ended
 * by @delimiter. */
static gchar *
make_paragraphs_ended_by (gsize        length,
                          const gchar *delimiter)
{
    GString *text = g_string_sized_new (length + 1);

    while (text->len < length)
    {
        g_string_append (text, "some ordinary line of text");
        g_string_append (text, delimiter);
    }

    return g_string_free (text, FALSE);
}

static gchar *
make_paragraphs (gsize length)
{
    return make_paragraphs_ended_by (length, "\n");
}

static void
never_submitted (AdwDialog   *dialog G_GNUC_UNUSED,
                 const gchar *text G_GNUC_UNUSED,
                 gpointer     user_data G_GNUC_UNUSED)
{
    g_assert_not_reached ();
}

/* Set when the dialog lets go of its caller's state: at once, when it refused
 * to open. */
static void
on_released (gpointer user_data)
{
    gboolean *released = user_data;

    *released = TRUE;
}

static GtkTextView *
find_text_view (GtkWidget *widget)
{
    if (GTK_IS_TEXT_VIEW (widget))
        return GTK_TEXT_VIEW (widget);

    for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    {
        GtkTextView *view = find_text_view (child);

        if (view)
            return view;
    }

    return NULL;
}

/* @user_data says whether the text handed to the dialog is one it should refuse
 * to open, answering as a cancellation would and putting nothing up. */
static void
open_with (gconstpointer user_data)
{
    gboolean expect_refusal = GPOINTER_TO_INT (user_data);
    g_autofree gchar *text = (expect_refusal) ? make_line (OVER_THE_LIMIT)
                                              : make_paragraphs (OVER_THE_LIMIT);
    gboolean released = FALSE;

    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    /* No reference of the test's own: GTK holds a toplevel until it is
     * destroyed, which is what every test here ends with. */
    AdwApplicationWindow *window = ADW_APPLICATION_WINDOW (adw_application_window_new (NULL));

    g_paste_gtk_util_text_dialog (GTK_WINDOW (window), "Edit Item", "Save", text, never_submitted, &released, on_released);

    AdwDialog *dialog = adw_application_window_get_visible_dialog (window);

    if (expect_refusal)
    {
        g_assert_true (released);
        g_assert_null (dialog);
        gtk_window_destroy (GTK_WINDOW (window));
        return;
    }

    g_assert_false (released);
    g_assert_nonnull (dialog);
    adw_dialog_force_close (dialog);
    gtk_window_destroy (GTK_WINDOW (window));
}

/* A line that long does not get in by being pasted either, which is the way
 * into a view that refused to open with one. */
static void
insert_a_long_line (void)
{
    g_autofree gchar *pasted = make_line (OVER_THE_LIMIT);

    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    AdwApplicationWindow *window = ADW_APPLICATION_WINDOW (adw_application_window_new (NULL));

    g_paste_gtk_util_text_dialog (GTK_WINDOW (window), "Edit Item", "Save", "short", never_submitted, NULL, NULL);

    AdwDialog *dialog = adw_application_window_get_visible_dialog (window);
    g_assert_nonnull (dialog);

    GtkTextView *view = find_text_view (GTK_WIDGET (dialog));
    g_assert_nonnull (view);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer (view);
    GtkTextIter iter;

    gtk_text_buffer_get_end_iter (buffer, &iter);
    gtk_text_buffer_insert (buffer, &iter, pasted, -1);
    g_assert_cmpint (gtk_text_buffer_get_char_count (buffer), ==, (gint) strlen ("short"));

    /* What fits still goes in, so the refusal is the length and not the paste. */
    g_autofree gchar *fits = make_line (UNDER_THE_LIMIT - strlen ("short"));

    gtk_text_buffer_get_end_iter (buffer, &iter);
    gtk_text_buffer_insert (buffer, &iter, fits, -1);
    g_assert_cmpint (gtk_text_buffer_get_char_count (buffer), ==, UNDER_THE_LIMIT);

    adw_dialog_force_close (dialog);
    gtk_window_destroy (GTK_WINDOW (window));
}

/* The long line need not be the first one pasted. Measuring only as far as the
 * first newline takes a paste starting with one for an insertion of nothing at
 * all, and lets everything behind it through. */
static void
insert_a_long_line_below (void)
{
    g_autofree gchar *line = make_line (OVER_THE_LIMIT);
    g_autofree gchar *pasted = g_strdup_printf ("\n%s\n", line);
    g_autofree gchar *paragraphs = make_paragraphs (OVER_THE_LIMIT);

    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    AdwApplicationWindow *window = ADW_APPLICATION_WINDOW (adw_application_window_new (NULL));

    g_paste_gtk_util_text_dialog (GTK_WINDOW (window), "Edit Item", "Save", "short", never_submitted, NULL, NULL);

    AdwDialog *dialog = adw_application_window_get_visible_dialog (window);
    g_assert_nonnull (dialog);

    GtkTextView *view = find_text_view (GTK_WIDGET (dialog));
    g_assert_nonnull (view);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer (view);
    GtkTextIter iter;

    gtk_text_buffer_get_end_iter (buffer, &iter);
    gtk_text_buffer_insert (buffer, &iter, pasted, -1);
    g_assert_cmpint (gtk_text_buffer_get_char_count (buffer), ==, (gint) strlen ("short"));

    /* Many lines, none of them long, still go in: the cost is a line and not a
     * paste. */
    gtk_text_buffer_get_end_iter (buffer, &iter);
    gtk_text_buffer_insert (buffer, &iter, paragraphs, -1);
    g_assert_cmpint (gtk_text_buffer_get_char_count (buffer),
                     ==,
                     (gint) (strlen ("short") + g_utf8_strlen (paragraphs, -1)));

    adw_dialog_force_close (dialog);
    gtk_window_destroy (GTK_WINDOW (window));
}

/* Lines broken by something other than a newline are lines all the same: the
 * view breaks them at every delimiter pango knows, so text broken by carriage
 * returns or by U+2029 is as many short lines to it, and opens. */
static void
open_other_delimiters (gconstpointer user_data)
{
    const gchar *delimiter = user_data;
    g_autofree gchar *text = make_paragraphs_ended_by (OVER_THE_LIMIT, delimiter);
    gboolean released = FALSE;

    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    AdwApplicationWindow *window = ADW_APPLICATION_WINDOW (adw_application_window_new (NULL));

    g_paste_gtk_util_text_dialog (GTK_WINDOW (window), "Edit Item", "Save", text, never_submitted, &released, on_released);

    AdwDialog *dialog = adw_application_window_get_visible_dialog (window);

    g_assert_false (released);
    g_assert_nonnull (dialog);
    adw_dialog_force_close (dialog);
    gtk_window_destroy (GTK_WINDOW (window));
}

/* Deleting the break between two lines joins them, and two lines under the limit
 * can make one over it: that is refused like the paste of one, while a join
 * that fits goes through. */
static void
join_into_a_long_line (void)
{
    g_autofree gchar *line = make_line (UNDER_THE_LIMIT);
    g_autofree gchar *text = g_strdup_printf ("%s\n%s\nshort\nshort", line, line);

    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    AdwApplicationWindow *window = ADW_APPLICATION_WINDOW (adw_application_window_new (NULL));

    g_paste_gtk_util_text_dialog (GTK_WINDOW (window), "Edit Item", "Save", text, never_submitted, NULL, NULL);

    AdwDialog *dialog = adw_application_window_get_visible_dialog (window);
    g_assert_nonnull (dialog);

    GtkTextView *view = find_text_view (GTK_WIDGET (dialog));
    g_assert_nonnull (view);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer (view);
    gint count = gtk_text_buffer_get_char_count (buffer);
    GtkTextIter start;
    GtkTextIter end;

    /* The break after the first long line. */
    gtk_text_buffer_get_iter_at_offset (buffer, &start, UNDER_THE_LIMIT);
    gtk_text_buffer_get_iter_at_offset (buffer, &end, UNDER_THE_LIMIT + 1);
    gtk_text_buffer_delete (buffer, &start, &end);
    g_assert_cmpint (gtk_text_buffer_get_char_count (buffer), ==, count);

    /* The break between the two short lines. */
    gtk_text_buffer_get_end_iter (buffer, &end);
    gtk_text_iter_backward_chars (&end, strlen ("short"));
    start = end;
    gtk_text_iter_backward_char (&start);
    gtk_text_buffer_delete (buffer, &start, &end);
    g_assert_cmpint (gtk_text_buffer_get_char_count (buffer), ==, count - 1);

    adw_dialog_force_close (dialog);
    gtk_window_destroy (GTK_WINDOW (window));
}

/* The replace of a selection, as a typed character makes one: its deletion,
 * then the insertion at the cursor, whatever the deletion did. */
static void
type_over_selection (GtkTextBuffer *buffer,
                     const gchar   *typed)
{
    gtk_text_buffer_begin_user_action (buffer);
    gtk_text_buffer_delete_selection (buffer, TRUE, TRUE);
    gtk_text_buffer_end_user_action (buffer);
    gtk_text_buffer_begin_user_action (buffer);
    gtk_text_buffer_insert_interactive_at_cursor (buffer, typed, -1, TRUE);
    gtk_text_buffer_end_user_action (buffer);
}

static void
on_paste_done (GtkTextBuffer *buffer    G_GNUC_UNUSED,
               GdkClipboard  *clipboard G_GNUC_UNUSED,
               gpointer       user_data)
{
    *((gboolean *) user_data) = TRUE;
}

/* The replace of a selection by a paste, through the view's own binding. */
static void
paste_over_selection (GtkTextView *view,
                      const gchar *pasted)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (view);
    gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;
    gboolean done = FALSE;
    gulong id = g_signal_connect (buffer, "paste-done", G_CALLBACK (on_paste_done), &done);

    gdk_clipboard_set_text (gtk_widget_get_clipboard (GTK_WIDGET (view)), pasted);
    g_signal_emit_by_name (view, "paste-clipboard");
    while (!done && g_get_monotonic_time () < deadline)
        g_main_context_iteration (NULL, FALSE);
    g_signal_handler_disconnect (buffer, id);
    g_assert_true (done);
}

/* Typing or pasting over a selection whose deletion would join two lines past
 * the limit is refused whole: the insertion does not land beside the text it
 * was meant to replace. A replace whose deletion fits goes through. */
static void
replace_into_a_long_line (void)
{
    g_autofree gchar *line = make_line (UNDER_THE_LIMIT);
    g_autofree gchar *text = g_strdup_printf ("%s\n%s\nshort\nshort", line, line);

    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    AdwApplicationWindow *window = ADW_APPLICATION_WINDOW (adw_application_window_new (NULL));

    g_paste_gtk_util_text_dialog (GTK_WINDOW (window), "Edit Item", "Save", text, never_submitted, NULL, NULL);

    AdwDialog *dialog = adw_application_window_get_visible_dialog (window);
    g_assert_nonnull (dialog);

    GtkTextView *view = find_text_view (GTK_WIDGET (dialog));
    g_assert_nonnull (view);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer (view);
    gint count = gtk_text_buffer_get_char_count (buffer);
    GtkTextIter start;
    GtkTextIter end;

    /* The break after the first long line. */
    gtk_text_buffer_get_iter_at_offset (buffer, &start, UNDER_THE_LIMIT);
    gtk_text_buffer_get_iter_at_offset (buffer, &end, UNDER_THE_LIMIT + 1);
    gtk_text_buffer_select_range (buffer, &start, &end);
    type_over_selection (buffer, "X");
    g_assert_cmpint (gtk_text_buffer_get_char_count (buffer), ==, count);

    gtk_text_buffer_get_iter_at_offset (buffer, &start, UNDER_THE_LIMIT);
    gtk_text_buffer_get_iter_at_offset (buffer, &end, UNDER_THE_LIMIT + 1);
    gtk_text_buffer_select_range (buffer, &start, &end);
    paste_over_selection (view, "X");
    g_assert_cmpint (gtk_text_buffer_get_char_count (buffer), ==, count);

    /* The break between the two short lines, replaced. */
    gtk_text_buffer_get_end_iter (buffer, &end);
    gtk_text_iter_backward_chars (&end, strlen ("short"));
    start = end;
    gtk_text_iter_backward_char (&start);
    gtk_text_buffer_select_range (buffer, &start, &end);
    type_over_selection (buffer, "X");
    g_assert_cmpint (gtk_text_buffer_get_char_count (buffer), ==, count);

    g_autofree gchar *all = NULL;

    gtk_text_buffer_get_bounds (buffer, &start, &end);
    all = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
    g_assert_true (g_str_has_suffix (all, "\nshortXshort"));

    adw_dialog_force_close (dialog);
    gtk_window_destroy (GTK_WINDOW (window));
}

static GtkWidget *
find_button (GtkWidget   *root,
             const gchar *label)
{
    if (GTK_IS_BUTTON (root) && !g_strcmp0 (gtk_button_get_label (GTK_BUTTON (root)), label))
        return root;

    for (GtkWidget *child = gtk_widget_get_first_child (root); child; child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = find_button (child, label);

        if (found)
            return found;
    }

    return NULL;
}

/* An empty text cannot be confirmed, whether the dialog opened on one or the
 * user emptied it. */
static void
empty_not_confirmed (void)
{

    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    AdwApplicationWindow *window = ADW_APPLICATION_WINDOW (adw_application_window_new (NULL));

    g_paste_gtk_util_text_dialog (GTK_WINDOW (window), "Add New Item", "Add", NULL, never_submitted, NULL, NULL);

    AdwDialog *dialog = adw_application_window_get_visible_dialog (window);
    GtkWidget *confirm = find_button (GTK_WIDGET (dialog), "Add");
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (find_text_view (GTK_WIDGET (dialog)));

    g_assert_false (gtk_widget_get_sensitive (confirm));
    gtk_text_buffer_set_text (buffer, "x", -1);
    g_assert_true (gtk_widget_get_sensitive (confirm));
    gtk_text_buffer_set_text (buffer, "", -1);
    g_assert_false (gtk_widget_get_sensitive (confirm));

    adw_dialog_force_close (dialog);
    gtk_window_destroy (GTK_WINDOW (window));
}

static void
count_answer (gboolean confirmed,
              gpointer user_data)
{
    gint *answers = user_data;

    ++answers[0];
    answers[1] = confirmed;
}

typedef enum
{
    CONFIRM_WINDOW_GONE,
    CONFIRM_ANSWERED_THEN_WINDOW_GONE,
    CONFIRM_HELD_WINDOW_GONE,
} ConfirmCase;

/* A question is answered exactly once: as a refusal when its window goes with
 * it up -- the dialog held by something else, which its own going does not
 * then answer, included -- and not again when its window goes after an
 * answer. */
static void
confirm_answered_once (gconstpointer user_data)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    ConfirmCase which = GPOINTER_TO_INT (user_data);
    gboolean respond = (which == CONFIRM_ANSWERED_THEN_WINDOW_GONE);
    GtkWindow *window = GTK_WINDOW (adw_window_new ());
    gint answers[2] = { 0, -1 };

    gtk_window_present (window);
    g_paste_gtk_util_confirm_dialog (window, "Go?", NULL, "Go", ADW_RESPONSE_DEFAULT, count_answer, answers);

    gpointer dialog = adw_window_get_visible_dialog (ADW_WINDOW (window));
    g_autoptr (AdwDialog) held = (which == CONFIRM_HELD_WINDOW_GONE) ? g_object_ref (dialog) : NULL;

    g_assert_nonnull (dialog);
    g_object_add_weak_pointer (dialog, &dialog);
    if (respond)
    {
        g_signal_emit_by_name (dialog, "response", "confirm");
        g_assert_cmpint (answers[0], ==, 1);
        g_assert_cmpint (answers[1], ==, TRUE);
    }

    gtk_window_destroy (window);
    while (g_main_context_iteration (NULL, FALSE));
    g_assert_cmpint (answers[0], ==, 1);
    g_assert_cmpint (answers[1], ==, respond);
    g_clear_object (&held);
    g_assert_null (dialog);
    g_assert_cmpint (answers[0], ==, 1);
}

/* A call refused for what it was handed is still answered, as refused: the
 * answer is what frees its caller's state. */
static void
confirm_refused_answers (gconstpointer user_data)
{
    const gchar *heading = user_data;

    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*CRITICAL*g_paste_gtk_util_confirm_dialog*");
        return;
    }

    gint answers[2] = { 0, -1 };

    g_log_set_always_fatal (G_LOG_LEVEL_ERROR);
    g_paste_gtk_util_confirm_dialog (NULL, heading, NULL, "Go", ADW_RESPONSE_DEFAULT, count_answer, answers);
    g_assert_cmpint (answers[0], ==, 1);
    g_assert_cmpint (answers[1], ==, FALSE);
}

/* Counted as they leave the client's connection, on GDBus' worker thread. */
static GDBusMessage *
count_calls (GDBusConnection *connection G_GNUC_UNUSED,
             GDBusMessage    *message,
             gboolean         incoming,
             gpointer         user_data)
{
    if (!incoming && g_dbus_message_get_message_type (message) == G_DBUS_MESSAGE_TYPE_METHOD_CALL &&
        !g_strcmp0 (g_dbus_message_get_member (message), "EmptyHistory"))
        g_atomic_int_inc ((gint *) user_data);

    return message;
}

/* Emptying a history confirmed once the daemon is gone sends nothing: the call
 * would be what the bus starts one on. */
static void
empty_confirmed_without_daemon (void)
{
    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    GtkWindow *window = GTK_WINDOW (adw_window_new ());

    g_assert_no_error (error);
    g_assert_cmpint (g_paste_client_get_daemon_presence (client), !=, G_PASTE_DAEMON_PRESENCE_READY);
    gtk_window_present (window);
    g_paste_settings_set_empty_history_confirmation (settings, TRUE);
    g_paste_gtk_util_empty_history (window, client, settings, "work");

    AdwDialog *dialog = adw_window_get_visible_dialog (ADW_WINDOW (window));

    GDBusConnection *connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (client));
    gint calls = 0;
    guint filter = g_dbus_connection_add_filter (connection, count_calls, &calls, NULL);

    g_assert_nonnull (dialog);
    g_signal_emit_by_name (dialog, "response", "confirm");

    /* A call made from the response has passed the filter, on its way out,
     * once everything queued on the connection is written. */
    g_assert_true (g_dbus_connection_flush_sync (connection, NULL, &error));
    g_assert_no_error (error);
    g_dbus_connection_remove_filter (connection, filter);
    g_assert_cmpint (g_atomic_int_get (&calls), ==, 0);
    g_paste_settings_reset (settings, G_PASTE_EMPTY_HISTORY_CONFIRMATION_SETTING);
    gtk_window_destroy (window);
}

/* A form held for its call can be neither closed nor confirmed again, the last
 * failure taken down; a failure gives it back with what went wrong, and a call
 * that went through closes it. One no longer presented is never held. */
static void
form_dialog_held (void)
{
    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    GtkWindow *window = GTK_WINDOW (adw_window_new ());
    GtkWidget *confirm;
    AdwDialog *dialog = g_paste_gtk_util_form_dialog ("Form", "Send", gtk_label_new ("content"), 300, 0, &confirm);
    g_autoptr (AdwDialog) held = g_object_ref (dialog);
    guint closed = 0;

    g_signal_connect_swapped (dialog, "closed", G_CALLBACK (g_paste_test_bus_count_emission), &closed);
    g_assert_false (g_paste_gtk_util_form_dialog_hold (dialog));
    gtk_window_present (window);
    adw_dialog_present (dialog, GTK_WIDGET (window));

    AdwBanner *banner = ADW_BANNER (g_paste_test_gtk_find_widget (GTK_WIDGET (dialog), ADW_TYPE_BANNER));

    g_assert_true (g_paste_gtk_util_form_dialog_hold (dialog));
    g_assert_true (g_paste_gtk_util_form_dialog_is_held (dialog));
    g_assert_false (g_paste_gtk_util_form_dialog_hold (dialog));
    g_assert_false (adw_dialog_get_can_close (dialog));
    g_assert_false (gtk_widget_get_sensitive (confirm));

    g_assert_true (g_paste_gtk_util_form_dialog_done (dialog, "Refused"));
    g_assert_false (g_paste_gtk_util_form_dialog_is_held (dialog));
    g_assert_true (adw_dialog_get_can_close (dialog));
    g_assert_true (adw_banner_get_revealed (banner));
    g_assert_cmpstr (adw_banner_get_title (banner), ==, "Refused");

    g_assert_true (g_paste_gtk_util_form_dialog_hold (dialog));
    g_assert_false (adw_banner_get_revealed (banner));
    g_assert_false (g_paste_gtk_util_form_dialog_done (dialog, NULL));
    g_assert_cmpuint (closed, ==, 1);
    g_assert_false (g_paste_gtk_util_form_dialog_hold (dialog));
    gtk_window_destroy (window);
}

/* A toast says what it was handed, an & a translation may hold included,
 * rather than nothing for markup Pango refuses. */
static void
toast_plain (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    GtkWindow *window = GTK_WINDOW (adw_application_window_new (NULL));
    GtkWidget *overlay = adw_toast_overlay_new ();
    GtkWidget *origin = gtk_label_new (NULL);

    adw_toast_overlay_set_child (ADW_TOAST_OVERLAY (overlay), origin);
    adw_application_window_set_content (ADW_APPLICATION_WINDOW (window), overlay);
    gtk_window_present (window);
    g_paste_gtk_util_toast (origin, "Fish & chips");
    g_assert_true (g_paste_test_gtk_shows_label (GTK_WIDGET (window), "Fish & chips"));
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
    g_test_add_data_func ("/ui/text_dialog/long_line_refused", GINT_TO_POINTER (TRUE), open_with);
    g_test_add_data_func ("/ui/text_dialog/many_lines_opened", GINT_TO_POINTER (FALSE), open_with);
    g_test_add_func ("/ui/text_dialog/long_line_not_pasted_in", insert_a_long_line);
    g_test_add_func ("/ui/text_dialog/long_line_below_not_pasted_in", insert_a_long_line_below);
    g_test_add_data_func ("/ui/text_dialog/cr_lines_opened", "\r", open_other_delimiters);
    g_test_add_data_func ("/ui/text_dialog/crlf_lines_opened", "\r\n", open_other_delimiters);
    g_test_add_data_func ("/ui/text_dialog/paragraph_separator_lines_opened", "\u2029", open_other_delimiters);
    g_test_add_func ("/ui/text_dialog/joined_line_refused", join_into_a_long_line);
    g_test_add_func ("/ui/text_dialog/joined_line_replace_refused", replace_into_a_long_line);
    g_test_add_func ("/ui/text_dialog/empty_not_confirmed", empty_not_confirmed);
    g_test_add_func ("/ui/form_dialog/held", form_dialog_held);
    g_test_add_func ("/ui/toast/plain", toast_plain);
    g_test_add_func ("/ui/confirm_dialog/empty_without_daemon", empty_confirmed_without_daemon);
    g_test_add_data_func ("/ui/confirm_dialog/window_gone", GINT_TO_POINTER (CONFIRM_WINDOW_GONE), confirm_answered_once);
    g_test_add_data_func ("/ui/confirm_dialog/answered_then_window_gone", GINT_TO_POINTER (CONFIRM_ANSWERED_THEN_WINDOW_GONE), confirm_answered_once);
    g_test_add_data_func ("/ui/confirm_dialog/held_window_gone", GINT_TO_POINTER (CONFIRM_HELD_WINDOW_GONE), confirm_answered_once);
    g_test_add_data_func ("/ui/confirm_dialog/refused/no-heading", NULL, confirm_refused_answers);
    g_test_add_data_func ("/ui/confirm_dialog/refused/invalid-heading", "\xff", confirm_refused_answers);
    return g_paste_test_env_run ();
}
