// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* The preferences' rows, on a private Xvfb: what a range row, a text row and a
 * shortcut row hand their setting, and what each leaves behind; and what the
 * storage buttons ask the daemon, and when. */

#include <gpaste-test-env.h>
#include <gpaste-3/gpaste-client.h>

#include "../../src/libgpaste/gpaste-gtk4/gpaste-gtk-preferences-group.c"
#include "../../src/libgpaste/gpaste-gtk4/gpaste-gtk-shortcut-row.c"

/* The client the storage buttons ask, faked: what the daemon's presence reads,
 * which calls went out, and the answer they wait for. */
static GPasteDaemonPresence fake_presence;
static guint                follows;
static guint                reexecutions;
static guint                passphrase_changes;
static GAsyncReadyCallback  pending_callback;
static gpointer             pending_data;
static GApplication        *application;
static gint                 holds;

/* What a storage row's handler asks for: the client, at once. */
static void
fake_client_new (GAsyncReadyCallback callback,
                 gpointer            user_data)
{
    callback (NULL, NULL, user_data);
}

static GPasteClient *
fake_new_finish (GAsyncResult *result G_GNUC_UNUSED,
                 GError      **error G_GNUC_UNUSED)
{
    return g_object_new (G_TYPE_OBJECT, NULL);
}

/* The wait for a daemon to serve, answered at once for one serving, and by the
 * case otherwise (answer_wait ()): what it waits on is libgpaste's
 * (/client/presence/wait-for-daemon). */
static GAsyncReadyCallback  wait_callback;
static gpointer             wait_data;
static GError              *wait_error;

static void
answer_wait (GError *error) /* (transfer full) (nullable) */
{
    GAsyncReadyCallback callback = g_steal_pointer (&wait_callback);

    g_assert_nonnull (callback);
    wait_error = error;
    callback (NULL, NULL, g_steal_pointer (&wait_data));
    g_clear_error (&wait_error);
}

static void
fake_wait_for_daemon (GPasteClient       *client G_GNUC_UNUSED,
                      GCancellable       *cancellable G_GNUC_UNUSED,
                      GAsyncReadyCallback callback,
                      gpointer            user_data)
{
    ++follows;
    wait_callback = callback;
    wait_data = user_data;
    if (fake_presence == G_PASTE_DAEMON_PRESENCE_READY)
        answer_wait (NULL);
}

static gboolean
fake_wait_for_daemon_finish (GAsyncResult *result G_GNUC_UNUSED,
                             GError      **error)
{
    if (!wait_error)
        return TRUE;

    g_propagate_error (error, g_error_copy (wait_error));

    return FALSE;
}

static void
fake_reexecute (GPasteClient       *client G_GNUC_UNUSED,
                GCancellable       *cancellable G_GNUC_UNUSED,
                GAsyncReadyCallback callback,
                gpointer            user_data)
{
    ++reexecutions;
    pending_callback = callback;
    pending_data = user_data;
}

static void
fake_change_passphrase (GPasteClient       *client G_GNUC_UNUSED,
                        GCancellable       *cancellable G_GNUC_UNUSED,
                        GAsyncReadyCallback callback,
                        gpointer            user_data)
{
    ++passphrase_changes;
    pending_callback = callback;
    pending_data = user_data;
}

static void
fake_finish (GPasteClient *client G_GNUC_UNUSED,
             GAsyncResult *result,
             GError      **error)
{
    g_task_propagate_boolean (G_TASK (result), error);
}

static GApplication *
fake_get_default (void)
{
    return application;
}

static void
counted_hold (GApplication *app G_GNUC_UNUSED)
{
    ++holds;
}

static void
counted_release (GApplication *app G_GNUC_UNUSED)
{
    --holds;
}

#define g_paste_client_new fake_client_new
#define g_paste_client_new_finish fake_new_finish
#define g_paste_util_wait_for_daemon fake_wait_for_daemon
#define g_paste_util_wait_for_daemon_finish fake_wait_for_daemon_finish
#define g_paste_client_reexecute fake_reexecute
#define g_paste_client_reexecute_finish fake_finish
#define g_paste_client_change_passphrase fake_change_passphrase
#define g_paste_client_change_passphrase_finish fake_finish
#define g_application_get_default fake_get_default
#define g_application_hold counted_hold
#define g_application_release counted_release
#define g_paste_gtk_preferences_history_settings_page_new test_history_settings_page_new
#include "../../src/libgpaste/gpaste-gtk4/gpaste-gtk-preferences-history-settings-page.c"
#undef g_paste_gtk_preferences_history_settings_page_new
#undef g_application_release
#undef g_application_hold
#undef g_application_get_default
#undef g_paste_client_change_passphrase_finish
#undef g_paste_client_change_passphrase
#undef g_paste_client_reexecute_finish
#undef g_paste_client_reexecute
#undef g_paste_util_wait_for_daemon_finish
#undef g_paste_util_wait_for_daemon
#undef g_paste_client_new_finish
#undef g_paste_client_new

static gboolean have_display;

/* @group in a window of its own, presented. */
static GtkWindow *
present_group (GPasteGtkPreferencesGroup *group)
{
    GtkWindow *window = GTK_WINDOW (gtk_window_new ());

    gtk_window_set_child (window, GTK_WIDGET (group));
    gtk_window_present (window);

    return window;
}

/* A range row commits only a value its range takes: a field emptied, or
 * holding no number, puts the setting's own back rather than the range's
 * minimum -- even where that minimum is 0, which emptiness would read as. */
static void
test_range_keeps_blank (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    GPasteGtkPreferencesGroup *group = g_paste_gtk_preferences_group_new ("Limits");
    AdwSpinRow *size = g_paste_gtk_preferences_group_add_range_setting (group, "Size", G_PASTE_MAX_HISTORY_SIZE_SETTING, 5, 65535, 5, settings);
    AdwSpinRow *element = g_paste_gtk_preferences_group_add_range_setting (group, "Element", G_PASTE_ELEMENT_SIZE_SETTING, 0, 511, 5, settings);
    GtkWindow *window = present_group (group);

    g_paste_settings_set_max_history_size (settings, 100);
    g_paste_settings_set_element_size (settings, 50);

    for (guint i = 0; i < 2; ++i)
    {
        const gchar *typed = (i) ? "abc" : "";

        gtk_editable_set_text (GTK_EDITABLE (size), typed);
        adw_spin_row_update (size);
        g_assert_cmpuint (g_paste_settings_get_max_history_size (settings), ==, 100);
        gtk_editable_set_text (GTK_EDITABLE (element), typed);
        adw_spin_row_update (element);
        g_assert_cmpuint (g_paste_settings_get_element_size (settings), ==, 50);
    }

    gtk_editable_set_text (GTK_EDITABLE (element), "70");
    adw_spin_row_update (element);
    g_assert_cmpuint (g_paste_settings_get_element_size (settings), ==, 70);

    gtk_window_destroy (window);
    g_paste_settings_reset (settings, G_PASTE_MAX_HISTORY_SIZE_SETTING);
    g_paste_settings_reset (settings, G_PASTE_ELEMENT_SIZE_SETTING);
}

/* What is typed in a text row reaches the setting once applied, not before. */
static void
test_text_applied (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    GPasteGtkPreferencesGroup *group = g_paste_gtk_preferences_group_new ("Upload");
    AdwEntryRow *row = g_paste_gtk_preferences_group_add_text_setting (group, "Command", G_PASTE_UPLOAD_COMMAND_SETTING, settings);
    GtkWindow *window = present_group (group);
    g_autofree gchar *original = g_strdup (g_paste_settings_get_upload_command (settings));

    gtk_editable_set_text (GTK_EDITABLE (row), "cur");
    g_assert_cmpstr (g_paste_settings_get_upload_command (settings), ==, original);
    gtk_editable_set_text (GTK_EDITABLE (row), "");
    g_assert_cmpstr (g_paste_settings_get_upload_command (settings), ==, original);
    gtk_editable_set_text (GTK_EDITABLE (row), "pastebinit");
    g_signal_emit_by_name (row, "apply");
    g_assert_cmpstr (g_paste_settings_get_upload_command (settings), ==, "pastebinit");

    gtk_window_destroy (window);
    g_paste_settings_reset (settings, G_PASTE_UPLOAD_COMMAND_SETTING);
}

/* Whether the apply button under @widget is up, the way AdwEntryRow shows it:
 * mapped. */
static gboolean
apply_shown (GtkWidget *widget)
{
    if (GTK_IS_BUTTON (widget) && !g_strcmp0 (gtk_button_get_icon_name (GTK_BUTTON (widget)), "adw-entry-apply-symbolic"))
        return gtk_widget_get_mapped (widget);

    for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    {
        if (apply_shown (child))
            return TRUE;
    }

    return FALSE;
}

static void
drain (void)
{
    for (guint i = 0; i < 20; ++i)
    {
        while (g_main_context_iteration (NULL, FALSE))
            ;
        g_usleep (10 * 1000);
    }
}

/* A write of the setting from elsewhere -- the row's own Reset to Default,
 * gpaste-client, dconf -- is shown as the setting's value, not as an edit the
 * user has yet to apply. */
static void
test_text_written_elsewhere (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    GPasteGtkPreferencesGroup *group = g_paste_gtk_preferences_group_new ("Upload");
    AdwEntryRow *row = g_paste_gtk_preferences_group_add_text_setting (group, "Command", G_PASTE_UPLOAD_COMMAND_SETTING, settings);
    GtkWindow *window = present_group (group);

    drain ();
    g_assert_false (apply_shown (GTK_WIDGET (row)));
    g_paste_settings_set_upload_command (settings, "written elsewhere");
    drain ();
    g_assert_cmpstr (gtk_editable_get_text (GTK_EDITABLE (row)), ==, "written elsewhere");
    g_assert_false (apply_shown (GTK_WIDGET (row)));

    gtk_editable_set_text (GTK_EDITABLE (row), "typed");
    drain ();
    g_assert_true (apply_shown (GTK_WIDGET (row)));

    gtk_window_destroy (window);
    g_paste_settings_reset (settings, G_PASTE_UPLOAD_COMMAND_SETTING);
}

static GtkWidget *
find_popover (GtkWidget *row)
{
    for (GtkWidget *child = gtk_widget_get_first_child (row); child; child = gtk_widget_get_next_sibling (child))
    {
        if (GTK_IS_POPOVER (child))
            return child;
    }

    return NULL;
}

/* A row going with its reset menu still up takes the menu with it: no row is
 * finalized with a popover still its child, which GTK warns about. */
static void
test_reset_menu_goes_with_row (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    GPasteGtkPreferencesGroup *group = g_paste_gtk_preferences_group_new ("Limits");
    AdwSpinRow *row = g_paste_gtk_preferences_group_add_range_setting (group, "Size", G_PASTE_MAX_HISTORY_SIZE_SETTING, 5, 65535, 5, settings);
    GtkWindow *window = present_group (group);
    g_autoptr (GtkGesture) gesture = gtk_gesture_click_new ();

    popup_reset_menu (gesture, GTK_WIDGET (row), 1, 1);
    g_assert_nonnull (find_popover (GTK_WIDGET (row)));
    gtk_window_destroy (window);
}

/* Shift is no modifier for a global shortcut: a key it is added to still types
 * or selects. A function key stands alone, Shift or not. */
static void
test_shortcut_shift_alone (void)
{
    g_assert_false (g_paste_gtk_shortcut_row_accepts (GDK_KEY_A, GDK_SHIFT_MASK));
    g_assert_false (g_paste_gtk_shortcut_row_accepts (GDK_KEY_exclam, GDK_SHIFT_MASK));
    g_assert_false (g_paste_gtk_shortcut_row_accepts (GDK_KEY_Return, GDK_SHIFT_MASK));
    g_assert_false (g_paste_gtk_shortcut_row_accepts (GDK_KEY_Left, GDK_SHIFT_MASK));
    g_assert_false (g_paste_gtk_shortcut_row_accepts (GDK_KEY_v, 0));
    g_assert_true (g_paste_gtk_shortcut_row_accepts (GDK_KEY_A, GDK_SHIFT_MASK | GDK_CONTROL_MASK));
    g_assert_true (g_paste_gtk_shortcut_row_accepts (GDK_KEY_v, GDK_SUPER_MASK));
    g_assert_true (g_paste_gtk_shortcut_row_accepts (GDK_KEY_F9, 0));
    g_assert_true (g_paste_gtk_shortcut_row_accepts (GDK_KEY_F9, GDK_SHIFT_MASK));

    /* What is captured is the key pressed: a keypad key is no main-row one,
     * Shift+Tab's ISO_Left_Tab is Tab. */
    g_assert_cmpuint (g_paste_keyval_canonicalize (GDK_KEY_KP_1), ==, GDK_KEY_KP_1);
    g_assert_cmpuint (g_paste_keyval_canonicalize (GDK_KEY_KP_Add), ==, GDK_KEY_KP_Add);
    g_assert_cmpuint (g_paste_keyval_canonicalize (GDK_KEY_ISO_Left_Tab), ==, GDK_KEY_Tab);
}

/* An armed row the focus leaves is disarmed, so only the row the keyboard is on
 * ever waits for a shortcut. */
static void
test_shortcut_disarmed_on_leave (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    GPasteGtkPreferencesGroup *group = g_paste_gtk_preferences_group_new ("Shortcuts");
    GtkWidget *first = g_paste_gtk_preferences_group_add_shortcut_setting (group, "Pop", G_PASTE_POP_SETTING, settings);
    GtkWidget *second = g_paste_gtk_preferences_group_add_shortcut_setting (group, "Show", G_PASTE_SHOW_HISTORY_SETTING, settings);
    GtkWindow *window = present_group (group);
    gboolean capturing;

    g_signal_emit_by_name (first, "activated");
    g_object_get (first, "capturing", &capturing, NULL);
    g_assert_true (capturing);

    gtk_widget_grab_focus (second);
    g_object_get (first, "capturing", &capturing, NULL);
    g_assert_false (capturing);
    g_assert_cmpstr (adw_action_row_get_subtitle (ADW_ACTION_ROW (first)), ==, "");

    gtk_window_destroy (window);
}

/* The accelerator stays what the row holds until it is finalized: a binding
 * reading it back as the row is disposed gets a string, not %NULL. */
static void
test_shortcut_disposed (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GtkWidget) row = g_object_ref_sink (g_paste_gtk_shortcut_row_new ("Pop"));

    g_paste_gtk_shortcut_row_set_accelerator (G_PASTE_GTK_SHORTCUT_ROW (row), "<Control>v");
    g_object_run_dispose (G_OBJECT (row));
    g_assert_cmpstr (g_paste_gtk_shortcut_row_get_accelerator (G_PASTE_GTK_SHORTCUT_ROW (row)), ==, "<Control>v");
}

static AdwButtonRow *
find_button (GtkWidget   *root,
             const gchar *title)
{
    if (ADW_IS_BUTTON_ROW (root) && !g_strcmp0 (adw_preferences_row_get_title (ADW_PREFERENCES_ROW (root)), title))
        return ADW_BUTTON_ROW (root);

    for (GtkWidget *child = gtk_widget_get_first_child (root); child; child = gtk_widget_get_next_sibling (child))
    {
        AdwButtonRow *found = find_button (child, title);

        if (found)
            return found;
    }

    return NULL;
}

/* The history page's storage rows, which the cases activate as a click does,
 * in a window a failure can be shown on -- or none, @window being %NULL. */
typedef struct
{
    GtkWidget    *page;
    AdwButtonRow *migration;
    AdwButtonRow *passphrase;
} StorageRows;

static StorageRows
present_storage_rows (GPasteSettings *settings,
                      GtkWindow     **window)
{
    StorageRows rows = { .page = GTK_WIDGET (test_history_settings_page_new (settings)) };

    if (window)
    {
        *window = GTK_WINDOW (adw_window_new ());
        adw_window_set_content (ADW_WINDOW (*window), rows.page);
        gtk_window_present (*window);
    }
    else
        g_object_ref_sink (rows.page);

    rows.migration = find_button (rows.page, "Change Storage Backend…");
    rows.passphrase = find_button (rows.page, "Change Passphrase…");
    g_assert_nonnull (rows.migration);
    g_assert_nonnull (rows.passphrase);

    return rows;
}

static void
activate (AdwButtonRow *row)
{
    g_signal_emit_by_name (row, "activated");
}

/* Answer the call the request sent, with @error if any. */
static void
answer (GError *error)
{
    g_autoptr (GTask) reply = g_task_new (NULL, NULL, NULL, NULL);

    if (error)
        g_task_return_error (reply, error);
    else
        g_task_return_boolean (reply, TRUE);
    pending_callback (NULL, G_ASYNC_RESULT (reply), pending_data);
    pending_callback = NULL;
}

static const gchar *
shown_failure (GtkWindow *window)
{
    AdwDialog *dialog = adw_window_get_visible_dialog (ADW_WINDOW (window));

    g_assert_true (ADW_IS_ALERT_DIALOG (dialog));

    return adw_alert_dialog_get_body (ADW_ALERT_DIALOG (dialog));
}

/* A migration the daemon refuses closes the gate it opened again, gives the
 * button back and says why. */
static void
test_storage_refused (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    GtkWindow *window;
    StorageRows rows = present_storage_rows (settings, &window);

    g_paste_settings_set_storage_backend_revision (settings, 37);
    fake_presence = G_PASTE_DAEMON_PRESENCE_READY;
    reexecutions = 0;
    activate (rows.migration);
    g_assert_cmpuint (reexecutions, ==, 1);
    g_assert_false (gtk_widget_get_sensitive (GTK_WIDGET (rows.migration)));
    answer (g_error_new_literal (G_PASTE_ERROR, G_PASTE_ERROR_FAILED, "Password selection could not be identified"));

    g_assert_cmpuint (g_paste_settings_get_storage_backend_revision (settings), ==, 37);
    g_assert_true (gtk_widget_get_sensitive (GTK_WIDGET (rows.migration)));
    g_assert_cmpstr (shown_failure (window), ==, "Password selection could not be identified");
    gtk_window_destroy (window);
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_REVISION_SETTING);
}

/* A migration that fails with the preferences already closed has no window to
 * put its dialog on. The failure still has to leave a trace, so it is warned
 * about -- run in a subprocess, the warning being fatal under g_test. */
static void
test_storage_refused_without_window_subprocess (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    StorageRows rows = present_storage_rows (settings, NULL);

    g_paste_settings_set_storage_backend_revision (settings, 37);
    fake_presence = G_PASTE_DAEMON_PRESENCE_READY;
    activate (rows.migration);
    answer (g_error_new_literal (G_PASTE_ERROR, G_PASTE_ERROR_FAILED, "Password selection could not be identified"));
    g_object_unref (rows.page);
}

static void
test_storage_refused_without_window (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_test_trap_subprocess ("/preferences/storage/refused-without-window/subprocess", 0, G_TEST_SUBPROCESS_DEFAULT);
    g_test_trap_assert_failed ();
    g_test_trap_assert_stderr ("*Could not change the storage backend: Password selection could not be identified*");
}

/* Nothing goes to a daemon that is not serving yet: the request waits for it,
 * the migration's gate unopened meanwhile, and gives up -- the button back,
 * the gate never opened -- when none comes. The application is held while the
 * request is out, so closing the window does not end the process before the
 * answer. */
static void
test_storage_waits_for_daemon (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    GtkWindow *window;
    StorageRows rows = present_storage_rows (settings, &window);

    g_paste_settings_set_storage_backend_revision (settings, 37);
    application = g_application_new ("org.gnome.GPaste.Test", G_APPLICATION_DEFAULT_FLAGS);
    reexecutions = follows = 0;
    fake_presence = G_PASTE_DAEMON_PRESENCE_STARTING;
    activate (rows.migration);
    g_assert_cmpint (holds, ==, 1);
    g_assert_cmpuint (follows, ==, 1);
    g_assert_cmpuint (reexecutions, ==, 0);
    g_assert_cmpuint (g_paste_settings_get_storage_backend_revision (settings), ==, 37);

    answer_wait (NULL);
    g_assert_cmpuint (reexecutions, ==, 1);
    g_assert_cmpint (holds, ==, 1);
    answer (NULL);
    g_assert_cmpint (holds, ==, 0);
    g_assert_true (gtk_widget_get_sensitive (GTK_WIDGET (rows.migration)));

    g_paste_settings_set_storage_backend_revision (settings, 37);
    fake_presence = G_PASTE_DAEMON_PRESENCE_STARTING;
    activate (rows.migration);
    answer_wait (g_error_new_literal (G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "No GPaste daemon is serving a history."));
    g_assert_cmpuint (reexecutions, ==, 1);
    g_assert_cmpuint (g_paste_settings_get_storage_backend_revision (settings), ==, 37);
    g_assert_cmpint (holds, ==, 0);
    g_assert_true (gtk_widget_get_sensitive (GTK_WIDGET (rows.migration)));
    g_assert_cmpstr (shown_failure (window), ==, "No GPaste daemon is serving a history.");

    gtk_window_destroy (window);
    g_clear_object (&application);
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_REVISION_SETTING);
}

/* A change of passphrase goes out asynchronously, and a refusal is shown as a
 * migration's is. */
static void
test_passphrase_refused (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    GtkWindow *window;

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);

    StorageRows rows = present_storage_rows (settings, &window);

    passphrase_changes = 0;
    fake_presence = G_PASTE_DAEMON_PRESENCE_READY;
    activate (rows.passphrase);
    g_assert_cmpuint (passphrase_changes, ==, 1);
    g_assert_false (gtk_widget_get_sensitive (GTK_WIDGET (rows.passphrase)));
    answer (g_error_new_literal (G_PASTE_ERROR, G_PASTE_ERROR_FAILED, "The passphrase could not be read"));
    g_assert_true (gtk_widget_get_sensitive (GTK_WIDGET (rows.passphrase)));
    g_assert_cmpstr (shown_failure (window), ==, "The passphrase could not be read");
    g_assert_cmpstr (adw_alert_dialog_get_heading (ADW_ALERT_DIALOG (adw_window_get_visible_dialog (ADW_WINDOW (window)))), ==, "Could Not Change Passphrase");
    gtk_window_destroy (window);
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
}

/* The passphrase row has a request out and a backend to follow, and answers to
 * both at once: a change of backend while the request is out leaves it
 * insensitive -- a second click would be a second request -- and the answer
 * gives it back only while the history is still encrypted: one moved to a
 * plain flavour meanwhile has no passphrase to change. */
static void
test_passphrase_sensitivity (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    GtkWindow *window;

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);

    StorageRows rows = present_storage_rows (settings, &window);

    g_assert_true (gtk_widget_get_sensitive (GTK_WIDGET (rows.passphrase)));
    passphrase_changes = 0;
    fake_presence = G_PASTE_DAEMON_PRESENCE_READY;
    activate (rows.passphrase);
    g_assert_false (gtk_widget_get_sensitive (GTK_WIDGET (rows.passphrase)));

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_SQLITE);
    g_assert_false (gtk_widget_get_sensitive (GTK_WIDGET (rows.passphrase)));

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);
    answer (g_error_new_literal (G_PASTE_ERROR, G_PASTE_ERROR_NOT_ENCRYPTED, "The history is not encrypted"));
    g_assert_cmpuint (passphrase_changes, ==, 1);
    g_assert_false (gtk_widget_get_sensitive (GTK_WIDGET (rows.passphrase)));
    g_assert_true (gtk_widget_get_sensitive (GTK_WIDGET (rows.migration)));

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
    g_assert_true (gtk_widget_get_sensitive (GTK_WIDGET (rows.passphrase)));
    gtk_window_destroy (window);
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
}

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DISPLAY);
    have_display = g_paste_test_env_has_display () && gtk_init_check ();
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    if (have_display)
        adw_init ();

    g_test_add_func ("/preferences/range/keeps-blank", test_range_keeps_blank);
    g_test_add_func ("/preferences/text/applied", test_text_applied);
    g_test_add_func ("/preferences/text/written-elsewhere", test_text_written_elsewhere);
    g_test_add_func ("/preferences/reset-menu/goes-with-row", test_reset_menu_goes_with_row);
    g_test_add_func ("/preferences/shortcut/shift-alone", test_shortcut_shift_alone);
    g_test_add_func ("/preferences/shortcut/disarmed-on-leave", test_shortcut_disarmed_on_leave);
    g_test_add_func ("/preferences/shortcut/disposed", test_shortcut_disposed);
    g_test_add_func ("/preferences/storage/refused", test_storage_refused);
    g_test_add_func ("/preferences/storage/refused-without-window", test_storage_refused_without_window);
    g_test_add_func ("/preferences/storage/refused-without-window/subprocess", test_storage_refused_without_window_subprocess);
    g_test_add_func ("/preferences/storage/waits-for-daemon", test_storage_waits_for_daemon);
    g_test_add_func ("/preferences/passphrase/refused", test_passphrase_refused);
    g_test_add_func ("/preferences/passphrase/sensitivity", test_passphrase_sensitivity);

    return g_paste_test_env_run ();
}
