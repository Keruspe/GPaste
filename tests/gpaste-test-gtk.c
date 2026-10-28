// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>
#include <gpaste-test-gtk.h>
#include <gpaste-3/gpaste-error.h>

/* The dialog over @window, either of the windows libadwaita hosts one in. */
static AdwDialog *
visible_dialog (GtkWindow *window)
{
    if (ADW_IS_APPLICATION_WINDOW (window))
        return adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (window));

    return adw_window_get_visible_dialog (ADW_WINDOW (window));
}

/* The first widget of @type in @root's tree, @root included, depth first. Each
 * lookup here walks the tree itself: one walk handed a predicate per lookup
 * would come out longer than the walks are. */
GtkWidget *
g_paste_test_gtk_find_widget (GtkWidget *root,
                              GType      type)
{
    if (G_TYPE_CHECK_INSTANCE_TYPE (root, type))
        return root;

    for (GtkWidget *child = gtk_widget_get_first_child (root); child; child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = g_paste_test_gtk_find_widget (child, type);

        if (found)
            return found;
    }

    return NULL;
}

/* As g_paste_test_gtk_find_widget (), for the button labelled @label. */
GtkWidget *
g_paste_test_gtk_find_button (GtkWidget   *root,
                              const gchar *label)
{
    if (GTK_IS_BUTTON (root) && !g_strcmp0 (gtk_button_get_label (GTK_BUTTON (root)), label))
        return root;

    for (GtkWidget *child = gtk_widget_get_first_child (root); child; child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = g_paste_test_gtk_find_button (child, label);

        if (found)
            return found;
    }

    return NULL;
}

static void
collect (GtkWidget *root,
         GType      type,
         GPtrArray *found)
{
    if (G_TYPE_CHECK_INSTANCE_TYPE (root, type))
        g_ptr_array_add (found, root);

    for (GtkWidget *child = gtk_widget_get_first_child (root); child; child = gtk_widget_get_next_sibling (child))
        collect (child, type, found);
}

/* Every widget of @type in @root's tree, @root included, depth first, in an
 * array that does not own them. */
GPtrArray *
g_paste_test_gtk_find_all (GtkWidget *root,
                           GType      type)
{
    GPtrArray *found = g_ptr_array_new ();

    collect (root, type, found);

    return found;
}

/* Whether a label or an inscription -- what a list row draws its text with --
 * in @root's tree, @root included, shows @text as written. */
gboolean
g_paste_test_gtk_shows_label (GtkWidget   *root,
                              const gchar *text)
{
    if (GTK_IS_LABEL (root) && !g_strcmp0 (gtk_label_get_text (GTK_LABEL (root)), text))
        return TRUE;
    if (GTK_IS_INSCRIPTION (root) && !g_strcmp0 (gtk_inscription_get_text (GTK_INSCRIPTION (root)), text))
        return TRUE;

    for (GtkWidget *child = gtk_widget_get_first_child (root); child; child = gtk_widget_get_next_sibling (child))
    {
        if (g_paste_test_gtk_shows_label (child, text))
            return TRUE;
    }

    return FALSE;
}

/* A GPasteTestBusDone for @widget being mapped, @arg unused. */
gboolean
g_paste_test_gtk_widget_mapped (gconstpointer widget,
                                gconstpointer arg G_GNUC_UNUSED)
{
    return gtk_widget_get_mapped ((GtkWidget *) widget);
}

static gboolean
dialog_closed (gconstpointer window,
               gconstpointer arg G_GNUC_UNUSED)
{
    return !visible_dialog ((GtkWindow *) window);
}

/* The dialog over @window, an AdwWindow or an AdwApplicationWindow, closed,
 * which a held one never is: waited for with a bound, so that one held for good
 * fails the case rather than hang it. */
void
g_paste_test_gtk_wait_dialog_closed (GtkWindow *window)
{
    g_paste_test_bus_wait_until (dialog_closed, window, NULL);
    g_assert_null (visible_dialog (window));
}

/* The buffer of the text view @dialog has to have. */
GtkTextBuffer *
g_paste_test_gtk_dialog_buffer (AdwDialog *dialog)
{
    GtkWidget *view = g_paste_test_gtk_find_widget (GTK_WIDGET (dialog), GTK_TYPE_TEXT_VIEW);

    g_assert_nonnull (view);

    return gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
}

gchar *
g_paste_test_gtk_dialog_text (AdwDialog *dialog)
{
    GtkTextBuffer *buffer = g_paste_test_gtk_dialog_buffer (dialog);
    GtkTextIter start, end;

    gtk_text_buffer_get_bounds (buffer, &start, &end);

    return gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
}

void
g_paste_test_gtk_set_dialog_text (AdwDialog   *dialog,
                                  const gchar *text)
{
    gtk_text_buffer_set_text (g_paste_test_gtk_dialog_buffer (dialog), text, -1);
}

/* What a suite's stand-in for the client's call does with the save. */
void
g_paste_test_gtk_save_send (GPasteTestGtkSave  *save,
                            GAsyncReadyCallback callback,
                            gpointer            data)
{
    ++save->sent;
    save->callback = callback;
    save->data = data;
}

/* What a suite's stand-in for the call's finish answers: @save's error, or
 * the uuid of the item saved. */
gchar *
g_paste_test_gtk_save_finish (GPasteTestGtkSave *save,
                              GError           **error)
{
    if (save->error)
    {
        g_propagate_error (error, g_steal_pointer (&save->error));
        return NULL;
    }

    return g_strdup ("00000000-0000-4000-8000-000000000002");
}

/* The save answered, as @client would hand the daemon's reply on. */
void
g_paste_test_gtk_save_answer (GPasteTestGtkSave *save,
                              gpointer           client)
{
    GAsyncReadyCallback callback = save->callback;

    g_assert_nonnull (callback);
    save->callback = NULL;
    callback (G_OBJECT (client), NULL, save->data);
}

/* @text written in @dialog and confirmed, @label being its confirm: the dialog
 * is held up while the save is out, and a refused one leaves the user what they
 * wrote, the failure said above it, for a second try to go through, each save
 * carrying @text (@save's text, which the suite's stand-in records). The
 * refusal's warning is expected through g_test_expect_message (), which sees
 * none from code built with G_LOG_USE_STRUCTURED: the suite compiles the
 * composer in without it. */
void
g_paste_test_gtk_assert_refusal_kept (GPasteTestGtkSave *save,
                                      gpointer           client,
                                      GtkWindow         *window,
                                      AdwDialog         *dialog,
                                      const gchar       *label,
                                      const gchar       *text,
                                      const gchar       *message)
{
    GtkWidget *confirm = g_paste_test_gtk_find_button (GTK_WIDGET (dialog), label);
    GtkWidget *cancel = g_paste_test_gtk_find_button (GTK_WIDGET (dialog), "Cancel");
    GtkWidget *view = g_paste_test_gtk_find_widget (GTK_WIDGET (dialog), GTK_TYPE_TEXT_VIEW);
    AdwBanner *banner = ADW_BANNER (g_paste_test_gtk_find_widget (GTK_WIDGET (dialog), ADW_TYPE_BANNER));
    g_autofree gchar *pattern = g_strdup_printf ("%s: refused", message);
    guint sent = save->sent;

    g_paste_test_gtk_set_dialog_text (dialog, text);
    g_clear_pointer (&save->text, g_free);
    g_signal_emit_by_name (confirm, "clicked");
    g_assert_cmpuint (save->sent, ==, sent + 1);
    g_assert_cmpstr (save->text, ==, text);
    {
        g_autofree gchar *written = g_paste_test_gtk_dialog_text (dialog);

        g_assert_cmpstr (written, ==, text);
    }

    g_assert_nonnull (save->callback);
    g_assert_false (gtk_widget_get_sensitive (confirm));
    /* Nor does a second press send it twice. */
    g_signal_emit_by_name (confirm, "clicked");
    g_assert_cmpuint (save->sent, ==, sent + 1);
    g_assert_false (gtk_widget_get_sensitive (cancel));
    g_assert_false (gtk_text_view_get_editable (GTK_TEXT_VIEW (view)));
    adw_dialog_close (dialog);
    g_assert_true (visible_dialog (window) == dialog);

    save->error = g_error_new_literal (G_PASTE_ERROR, G_PASTE_ERROR_REJECTED, "refused");
    g_test_expect_message ("GPaste", G_LOG_LEVEL_WARNING, pattern);
    g_paste_test_gtk_save_answer (save, client);
    g_test_assert_expected_messages ();
    g_assert_true (visible_dialog (window) == dialog);
    g_assert_true (adw_banner_get_revealed (banner));
    g_assert_cmpstr (adw_banner_get_title (banner), ==, message);
    g_assert_true (gtk_widget_get_sensitive (confirm));
    g_assert_true (gtk_widget_get_sensitive (cancel));
    g_assert_true (gtk_text_view_get_editable (GTK_TEXT_VIEW (view)));
    {
        g_autofree gchar *kept = g_paste_test_gtk_dialog_text (dialog);

        g_assert_cmpstr (kept, ==, text);
    }

    /* Again, and through this time. */
    g_clear_pointer (&save->text, g_free);
    g_signal_emit_by_name (confirm, "clicked");
    g_assert_cmpuint (save->sent, ==, sent + 2);
    g_assert_cmpstr (save->text, ==, text);
    g_assert_false (adw_banner_get_revealed (banner));
    g_paste_test_gtk_save_answer (save, client);
    g_paste_test_gtk_wait_dialog_closed (window);
}
