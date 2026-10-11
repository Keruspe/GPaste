// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* The standalone daemon's libadwaita prompt, driven through the GPastePrompt
 * calls the storage layer makes, on a private Xvfb: what each dialog offers,
 * what it answers, and that its text reaches the screen as written. */

#include <gpaste-test-env.h>
#include <gpaste-test-bus.h>
#include <gpaste-daemon/gpaste-prompt.h>

/* The private bus has no keyring, and looking one up warns, which g_test makes
 * fatal: the remember switch starts off, with nothing remembered. */
static void
no_keyring (GPasteStorageRemember remember G_GNUC_UNUSED,
            gboolean             *starts_on,
            gboolean             *can_forget)
{
    *starts_on = FALSE;
    *can_forget = FALSE;
}

#define g_paste_prompt_remember_state no_keyring
#include <gpaste-prompt-adw.c>
#undef g_paste_prompt_remember_state

#include <gpaste-daemon/gpaste-passphrase.h>

static gboolean have_display;
static GtkApplication *app;

typedef struct
{
    gboolean              done;
    gboolean              confirmed;
    GPasteStorage         chosen;
    gboolean              import;
    gboolean              cleanup;
    GPastePassphrase     *passphrase;
    GPasteStorageRemember remember;
} Answer;

static void
on_migration_answered (GObject      *source,
                       GAsyncResult *result,
                       gpointer      user_data)
{
    Answer *answer = user_data;
    g_autoptr (GError) error = NULL;

    answer->confirmed = g_paste_prompt_migration_finish (G_PASTE_PROMPT (source), result, &answer->chosen,
                                                         &answer->import, &answer->cleanup, &error);
    g_assert_no_error (error);
    answer->done = TRUE;
}

static void
on_passphrase_answered (GObject      *source,
                        GAsyncResult *result,
                        gpointer      user_data)
{
    Answer *answer = user_data;
    g_autoptr (GError) error = NULL;

    answer->passphrase = g_paste_prompt_passphrase_finish (G_PASTE_PROMPT (source), result, &answer->remember, &error);
    g_assert_no_error (error);
    answer->done = TRUE;
}

static gboolean
answered (gconstpointer data,
          gconstpointer arg G_GNUC_UNUSED)
{
    return ((const Answer *) data)->done;
}

static void
wait_for (Answer *answer)
{
    g_paste_test_bus_wait_until (answered, answer, NULL);
    g_assert_true (answer->done);
}

/* The window the prompt has just put up: the application's newest. */
static GtkWindow *
shown_window (void)
{
    GList *windows = gtk_application_get_windows (app);

    g_assert_nonnull (windows);

    return windows->data;
}

static void
collect (GtkWidget *widget,
         GType      type,
         GPtrArray *found)
{
    if (G_TYPE_CHECK_INSTANCE_TYPE (widget, type))
        g_ptr_array_add (found, widget);

    for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
        collect (child, type, found);
}

static GPtrArray *
find_all (GtkWindow *window,
          GType      type)
{
    GPtrArray *found = g_ptr_array_new ();

    collect (GTK_WIDGET (window), type, found);

    return found;
}

/* Whether some label shows @text, markup parsed or not. */
static gboolean
shows (GtkWindow   *window,
       const gchar *text)
{
    g_autoptr (GPtrArray) labels = find_all (window, GTK_TYPE_LABEL);

    for (guint i = 0; i < labels->len; ++i)
    {
        if (g_strrstr (gtk_label_get_text (labels->pdata[i]), text))
            return TRUE;
    }

    return FALSE;
}

/* Whether a button says @label: the Keep button's words are quoted in the
 * description too. */
static gboolean
has_button (GtkWindow   *window,
            const gchar *label)
{
    g_autoptr (GPtrArray) buttons = find_all (window, GTK_TYPE_BUTTON);

    for (guint i = 0; i < buttons->len; ++i)
    {
        if (!g_strcmp0 (gtk_button_get_label (buttons->pdata[i]), label))
            return TRUE;
    }

    return FALSE;
}

/* Whether a group of @window describes itself with @text, as the dialog sets
 * it: escaped. */
static gboolean
has_description (GtkWindow   *window,
                 const gchar *text)
{
    g_autoptr (GPtrArray) groups = find_all (window, ADW_TYPE_PREFERENCES_GROUP);
    g_autofree gchar *escaped = g_markup_escape_text (text, -1);

    for (guint i = 0; i < groups->len; ++i)
    {
        if (!g_strcmp0 (adw_preferences_group_get_description (groups->pdata[i]), escaped))
            return TRUE;
    }

    return FALSE;
}

/* No row or banner takes what it shows for markup: the prompt's text is plain. */
static void
assert_plain_rows (GtkWindow *window)
{
    g_autoptr (GPtrArray) rows = find_all (window, ADW_TYPE_PREFERENCES_ROW);
    g_autoptr (GPtrArray) banners = find_all (window, ADW_TYPE_BANNER);

    g_assert_cmpuint (rows->len, >, 0);
    for (guint i = 0; i < rows->len; ++i)
        g_assert_false (adw_preferences_row_get_use_markup (rows->pdata[i]));
    for (guint i = 0; i < banners->len; ++i)
        g_assert_false (adw_banner_get_use_markup (banners->pdata[i]));
}

static const GPasteStorage offered[] = { G_PASTE_STORAGE_FILE, G_PASTE_STORAGE_NOOP };

#if defined(G_PASTE_ENABLE_ENCRYPTION) && defined(G_PASTE_ENABLE_SQLITE)
/* Picking another backend keeps what was ticked: GTK unpicks the one before
 * first, and that moment, nothing picked, turned both switches off. */
static void
test_migration_switch_keeps_toggles (void)
{
    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    static const GPasteStorage backends[] = { G_PASTE_STORAGE_FILE, G_PASTE_STORAGE_SQLITE, G_PASTE_STORAGE_ENCRYPTED_FILE };
    g_autoptr (GPastePrompt) prompt = g_paste_prompt_adw_new (app);
    Answer answer = { 0 };

    g_paste_prompt_migration_async (prompt, backends, G_N_ELEMENTS (backends), G_PASTE_STORAGE_FILE, on_migration_answered, &answer);

    GtkWindow *window = shown_window ();
    MigrationDialog *dialog = g_object_get_data (G_OBJECT (window), "gpaste-migration-dialog");

    gtk_check_button_set_active (dialog->backend_checks[1], TRUE);
    adw_switch_row_set_active (dialog->import_row, TRUE);
    adw_switch_row_set_active (dialog->cleanup_row, TRUE);
    gtk_check_button_set_active (dialog->backend_checks[2], TRUE);
    g_assert_true (adw_switch_row_get_active (dialog->import_row));
    g_assert_true (adw_switch_row_get_active (dialog->cleanup_row));

    g_signal_emit_by_name (dialog->apply, "clicked");
    wait_for (&answer);
    g_assert_true (answer.confirmed);
    g_assert_cmpint (answer.chosen, ==, G_PASTE_STORAGE_ENCRYPTED_FILE);
}
#endif

/* The backend in use is picked, and Keep is offered; another picked instead
 * unpicks it, and Apply answers with that one. */
static void
test_migration_current_picked (void)
{
    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    g_autoptr (GPastePrompt) prompt = g_paste_prompt_adw_new (app);
    Answer answer = { 0 };

    g_paste_prompt_migration_async (prompt, offered, G_N_ELEMENTS (offered), G_PASTE_STORAGE_FILE, on_migration_answered, &answer);

    GtkWindow *window = shown_window ();
    MigrationDialog *dialog = g_object_get_data (G_OBJECT (window), "gpaste-migration-dialog");

    assert_plain_rows (window);
    g_assert_false (adw_banner_get_use_markup (dialog->warning));
    g_assert_true (gtk_check_button_get_active (dialog->backend_checks[0]));
    g_assert_false (gtk_check_button_get_active (dialog->backend_checks[1]));
    g_assert_true (gtk_widget_get_sensitive (dialog->apply));
    g_assert_true (has_button (window, g_paste_prompt_text (G_PASTE_PROMPT_TEXT_MIGRATION_KEEP)));
    g_assert_true (has_description (window, g_paste_prompt_text (G_PASTE_PROMPT_TEXT_MIGRATION_DESCRIPTION)));

    gtk_check_button_set_active (dialog->backend_checks[1], TRUE);
    g_assert_false (gtk_check_button_get_active (dialog->backend_checks[0]));
    g_signal_emit_by_name (dialog->apply, "clicked");
    wait_for (&answer);
    g_assert_true (answer.confirmed);
    g_assert_cmpint (answer.chosen, ==, G_PASTE_STORAGE_NOOP);
}

/* A backend in use this build cannot construct is not on the list: nothing is
 * picked for the user, there is nothing to keep, and Apply answers nothing
 * until they pick one. */
static void
test_migration_nothing_picked (void)
{
    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    g_autoptr (GPastePrompt) prompt = g_paste_prompt_adw_new (app);
    Answer answer = { 0 };

    g_paste_prompt_migration_async (prompt, offered, G_N_ELEMENTS (offered), G_PASTE_STORAGE_SQLITE, on_migration_answered, &answer);

    GtkWindow *window = shown_window ();
    MigrationDialog *dialog = g_object_get_data (G_OBJECT (window), "gpaste-migration-dialog");

    for (guint i = 0; i < G_N_ELEMENTS (offered); ++i)
        g_assert_false (gtk_check_button_get_active (dialog->backend_checks[i]));
    g_assert_false (gtk_widget_get_sensitive (dialog->apply));
    g_assert_false (has_button (window, g_paste_prompt_text (G_PASTE_PROMPT_TEXT_MIGRATION_KEEP)));
    /* Nor is the button named. */
    g_assert_true (has_description (window, g_paste_prompt_text (G_PASTE_PROMPT_TEXT_MIGRATION_DESCRIPTION_UNKEPT)));
    on_apply (NULL, dialog);
    g_main_context_iteration (NULL, FALSE);
    g_assert_false (answer.done);

    gtk_check_button_set_active (dialog->backend_checks[0], TRUE);
    g_assert_true (gtk_widget_get_sensitive (dialog->apply));
    g_signal_emit_by_name (dialog->apply, "clicked");
    wait_for (&answer);
    g_assert_true (answer.confirmed);
    g_assert_cmpint (answer.chosen, ==, G_PASTE_STORAGE_FILE);
}

/* Losing the window, however that happens, is a dismissal, not a failure. */
static void
test_migration_dismissed (void)
{
    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    g_autoptr (GPastePrompt) prompt = g_paste_prompt_adw_new (app);
    Answer answer = { 0 };

    g_paste_prompt_migration_async (prompt, offered, G_N_ELEMENTS (offered), G_PASTE_STORAGE_FILE, on_migration_answered, &answer);
    gtk_window_destroy (shown_window ());
    wait_for (&answer);
    g_assert_false (answer.confirmed);
}

/* What went wrong is shown as written, beside the standing explanation, a "&"
 * or a "<" in it included. */
static void
test_passphrase_error_shown (void)
{
    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    static const gchar *error = "Could not open \"a & b <c>\"";
    g_autoptr (GPastePrompt) prompt = g_paste_prompt_adw_new (app);
    Answer answer = { 0 };

    g_paste_prompt_passphrase_async (prompt, FALSE, error, G_PASTE_STORAGE_REMEMBER_UNCHANGED, on_passphrase_answered, &answer);

    GtkWindow *window = shown_window ();

    assert_plain_rows (window);
    g_assert_true (shows (window, error));
    g_assert_true (shows (window, g_paste_prompt_text (G_PASTE_PROMPT_TEXT_PASSPHRASE_DESCRIPTION)));

    gtk_window_destroy (window);
    wait_for (&answer);
    g_assert_null (answer.passphrase);
}

/* A new passphrase is taken once typed twice alike, Enter in either entry
 * answering then and only then: @user_data says whether the confirmation's
 * answers, or the passphrase's own. */
static void
test_passphrase_confirmed (gconstpointer user_data)
{
    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    g_autoptr (GPastePrompt) prompt = g_paste_prompt_adw_new (app);
    Answer answer = { 0 };

    g_paste_prompt_passphrase_async (prompt, TRUE, NULL, G_PASTE_STORAGE_REMEMBER_UNCHANGED, on_passphrase_answered, &answer);

    GtkWindow *window = shown_window ();
    PassphraseDialog *dialog = g_object_get_data (G_OBJECT (window), "gpaste-passphrase-dialog");

    assert_plain_rows (window);
    g_assert_false (gtk_widget_get_sensitive (dialog->ok));
    gtk_editable_set_text (dialog->entry, "correct horse");
    gtk_editable_set_text (dialog->confirm_entry, "correct hors");
    g_assert_false (gtk_widget_get_sensitive (dialog->ok));
    g_signal_emit_by_name (dialog->entry, "entry-activated");
    g_signal_emit_by_name (dialog->confirm_entry, "entry-activated");
    g_main_context_iteration (NULL, FALSE);
    g_assert_false (answer.done);

    gtk_editable_set_text (dialog->confirm_entry, "correct horse");
    g_assert_true (gtk_widget_get_sensitive (dialog->ok));
    g_signal_emit_by_name ((GPOINTER_TO_INT (user_data)) ? dialog->confirm_entry : dialog->entry, "entry-activated");
    wait_for (&answer);
    g_assert_nonnull (answer.passphrase);
    g_assert_cmpstr (g_paste_passphrase_peek (answer.passphrase), ==, "correct horse");
    g_clear_pointer (&answer.passphrase, g_paste_passphrase_free);
}

/* A report says what it was given, a "&" in it included. */
static void
test_report_shown (void)
{
    if (!have_display)
    {
        g_test_skip ("no display");
        return;
    }

    static const gchar *message = "History \"a & b <c>\" was not deleted";
    g_autoptr (GPastePrompt) prompt = g_paste_prompt_adw_new (app);

    g_paste_prompt_report (prompt, "Title", message);

    GtkWindow *window = shown_window ();

    g_assert_true (shows (window, message));
    gtk_window_destroy (window);
}

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DISPLAY);
    have_display = g_paste_test_env_has_display () && gtk_init_check ();
    g_test_init (&argc, &argv, NULL);

    if (have_display)
    {
        g_autoptr (GError) error = NULL;

        adw_init ();
        app = GTK_APPLICATION (adw_application_new ("org.gnome.GPaste.Daemon.Test", G_APPLICATION_NON_UNIQUE));
        g_application_register (G_APPLICATION (app), NULL, &error);
        g_assert_no_error (error);
    }

    g_test_add_func ("/prompt-adw/migration/current-picked", test_migration_current_picked);
#if defined(G_PASTE_ENABLE_ENCRYPTION) && defined(G_PASTE_ENABLE_SQLITE)
    g_test_add_func ("/prompt-adw/migration/switch-keeps-toggles", test_migration_switch_keeps_toggles);
#endif
    g_test_add_func ("/prompt-adw/migration/nothing-picked", test_migration_nothing_picked);
    g_test_add_func ("/prompt-adw/migration/dismissed", test_migration_dismissed);
    g_test_add_func ("/prompt-adw/passphrase/error-shown", test_passphrase_error_shown);
    g_test_add_data_func ("/prompt-adw/passphrase/confirmed", GINT_TO_POINTER (TRUE), test_passphrase_confirmed);
    g_test_add_data_func ("/prompt-adw/passphrase/confirmed_first_entry", GINT_TO_POINTER (FALSE), test_passphrase_confirmed);
    g_test_add_func ("/prompt-adw/report/shown", test_report_shown);

    return g_paste_test_env_run ();
}
