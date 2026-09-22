// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-env.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>

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
on_answer (const gchar *text,
           gpointer     user_data)
{
    gboolean *refused = user_data;

    *refused = !text;
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
    gboolean refused = FALSE;

    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    /* No reference of the test's own: GTK holds a toplevel until it is
     * destroyed, which is what every test here ends with. */
    AdwApplicationWindow *window = ADW_APPLICATION_WINDOW (adw_application_window_new (NULL));

    g_paste_gtk_util_text_dialog (GTK_WINDOW (window), "Edit Item", "Save", text, on_answer, &refused);

    AdwDialog *dialog = adw_application_window_get_visible_dialog (window);

    if (expect_refusal)
    {
        g_assert_true (refused);
        g_assert_null (dialog);
        gtk_window_destroy (GTK_WINDOW (window));
        return;
    }

    g_assert_false (refused);
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
    gboolean refused = FALSE;

    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    AdwApplicationWindow *window = ADW_APPLICATION_WINDOW (adw_application_window_new (NULL));

    g_paste_gtk_util_text_dialog (GTK_WINDOW (window), "Edit Item", "Save", "short", on_answer, &refused);

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
    gboolean refused = FALSE;

    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    AdwApplicationWindow *window = ADW_APPLICATION_WINDOW (adw_application_window_new (NULL));

    g_paste_gtk_util_text_dialog (GTK_WINDOW (window), "Edit Item", "Save", "short", on_answer, &refused);

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
    gboolean refused = FALSE;

    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    AdwApplicationWindow *window = ADW_APPLICATION_WINDOW (adw_application_window_new (NULL));

    g_paste_gtk_util_text_dialog (GTK_WINDOW (window), "Edit Item", "Save", text, on_answer, &refused);

    AdwDialog *dialog = adw_application_window_get_visible_dialog (window);

    g_assert_false (refused);
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
    gboolean refused = FALSE;

    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    AdwApplicationWindow *window = ADW_APPLICATION_WINDOW (adw_application_window_new (NULL));

    g_paste_gtk_util_text_dialog (GTK_WINDOW (window), "Edit Item", "Save", text, on_answer, &refused);

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
    return g_paste_test_env_run ();
}
