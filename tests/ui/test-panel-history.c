// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>
#include <gpaste-test-env.h>

/* Reach the row the sidebar draws the history on, and the revealer on it. */
#include <gpaste-ui-panel-history.c>
/* And the row an item is drawn on, for its star. */
#include <gpaste-ui-item.c>
#include <gpaste-3/gpaste-settings.h>

/* The calls the sidebar makes, answered when the test says so: @sent_name is
 * the history named, and the answer carries @answer_error if one is set. */
static gchar *sent_name;
static GAsyncReadyCallback sent_callback;
static gpointer sent_data;
static GError *answer_error;

static void
send_switch (GPasteClient       *client G_GNUC_UNUSED,
             const gchar        *name,
             GCancellable       *cancellable G_GNUC_UNUSED,
             GAsyncReadyCallback callback,
             gpointer            data)
{
    g_set_str (&sent_name, name);
    sent_callback = callback;
    sent_data = data;
}

static void
send_backup (GPasteClient       *client,
             const gchar        *history G_GNUC_UNUSED,
             const gchar        *backup,
             GCancellable       *cancellable,
             GAsyncReadyCallback callback,
             gpointer            data)
{
    send_switch (client, backup, cancellable, callback, data);
}

static void
finish_call (GPasteClient *client G_GNUC_UNUSED,
             GAsyncResult *result G_GNUC_UNUSED,
             GError      **error)
{
    if (answer_error)
        g_propagate_error (error, g_steal_pointer (&answer_error));
}

/* And the sidebar, for what it sends. */
#define g_paste_client_switch_history send_switch
#define g_paste_client_switch_history_finish finish_call
#define g_paste_client_backup_history send_backup
#define g_paste_client_backup_history_finish finish_call
#include <gpaste-ui-panel.c>
#undef g_paste_client_backup_history_finish
#undef g_paste_client_backup_history
#undef g_paste_client_switch_history_finish
#undef g_paste_client_switch_history

static void
answer (GPasteClient *client)
{
    GAsyncReadyCallback callback = sent_callback;

    g_assert_nonnull (callback);
    sent_callback = NULL;
    callback (G_OBJECT (client), NULL, sent_data);
}

static gboolean have_display;

static gboolean
revealed (GPasteUiPanelHistory *self)
{
    return gtk_revealer_get_reveal_child (GTK_REVEALER (self->revealer));
}

static gboolean
row_found (gconstpointer item,
           gconstpointer arg G_GNUC_UNUSED)
{
    return ((GPasteUiPanelHistory *) item)->row != NULL;
}

/* A history row in a sidebar of its own, presented, and mapped far enough for
 * the row to have been found. The sidebar owns the section and the section the
 * item, both appended with their reference. */
static GtkWidget *
row_window (GPasteUiPanelHistory **history)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);

    g_assert_no_error (error);

    GtkWidget *sidebar = adw_sidebar_new ();
    AdwSidebarSection *section = adw_sidebar_section_new ();
    GPasteUiPanelHistory *item = G_PASTE_UI_PANEL_HISTORY (g_paste_ui_panel_history_new (client, "work", 3));

    adw_sidebar_section_append (section, ADW_SIDEBAR_ITEM (item));
    adw_sidebar_append (ADW_SIDEBAR (sidebar), section);

    GtkWidget *window = gtk_window_new ();

    gtk_window_set_child (GTK_WINDOW (window), sidebar);
    gtk_window_present (GTK_WINDOW (window));

    g_paste_test_bus_wait_until (row_found, item, NULL);
    g_assert_nonnull (item->row);

    *history = item;

    return window;
}

/* The button shows for the pointer, and for the keyboard -- but a click focuses
 * the row too, and must not leave the button up once the pointer has moved on. */
static void
test_revealed (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    GPasteUiPanelHistory *history;
    GtkWidget *window = row_window (&history);
    GtkWidget *row = history->row;

    gtk_widget_unset_state_flags (row, GTK_STATE_FLAG_PRELIGHT | GTK_STATE_FLAG_FOCUSED | GTK_STATE_FLAG_FOCUS_WITHIN | GTK_STATE_FLAG_FOCUS_VISIBLE);
    g_assert_false (revealed (history));

    gtk_widget_set_state_flags (row, GTK_STATE_FLAG_PRELIGHT, FALSE);
    g_assert_true (revealed (history));
    gtk_widget_unset_state_flags (row, GTK_STATE_FLAG_PRELIGHT);
    g_assert_false (revealed (history));

    /* What a click leaves behind. */
    gtk_widget_set_state_flags (row, GTK_STATE_FLAG_FOCUSED | GTK_STATE_FLAG_FOCUS_WITHIN, FALSE);
    g_assert_false (revealed (history));

    /* What the keyboard adds to it. */
    gtk_widget_set_state_flags (row, GTK_STATE_FLAG_FOCUS_VISIBLE, FALSE);
    g_assert_true (revealed (history));

    gtk_window_destroy (GTK_WINDOW (window));
}

static gchar *
row_shortcuts (GtkWidget *row)
{
    g_autoptr (GString) triggers = g_string_new (NULL);
    g_autoptr (GListModel) controllers = gtk_widget_observe_controllers (row);

    for (guint i = 0; i < g_list_model_get_n_items (controllers); ++i)
    {
        g_autoptr (GObject) controller = g_list_model_get_item (controllers, i);

        if (!GTK_IS_SHORTCUT_CONTROLLER (controller))
            continue;

        for (guint j = 0; j < g_list_model_get_n_items (G_LIST_MODEL (controller)); ++j)
        {
            g_autoptr (GtkShortcut) shortcut = g_list_model_get_item (G_LIST_MODEL (controller), j);
            g_autofree gchar *trigger = gtk_shortcut_trigger_to_string (gtk_shortcut_get_trigger (shortcut));

            g_string_append_printf (triggers, "%s;", trigger);
        }
    }

    return g_string_free (g_steal_pointer (&triggers), FALSE);
}

/* Delete deletes the focused history; BackSpace is left meaning back. */
static void
test_delete_key (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    GPasteUiPanelHistory *history;
    GtkWidget *window = row_window (&history);
    g_autofree gchar *triggers = row_shortcuts (history->row);

    g_assert_nonnull (strstr (triggers, "Delete"));
    g_assert_null (strstr (triggers, "BackSpace"));

    gtk_window_destroy (GTK_WINDOW (window));
}

static void
count_notify (GObject    *object G_GNUC_UNUSED,
              GParamSpec *pspec  G_GNUC_UNUSED,
              gpointer    user_data)
{
    ++*(guint *) user_data;
}

/* A row's selection mode notifies when it moves, and only then: it is bound
 * from the list's on every row, each toggle reaching all of them. */
static void
test_item_selection_mode_notify (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    GtkWidget *window = gtk_window_new ();
    g_autoptr (GtkWidget) item = g_object_ref_sink (g_paste_ui_item_new (client, settings, GTK_WINDOW (window), 0));
    guint notified = 0;

    g_assert_no_error (error);
    g_signal_connect (item, "notify::selection-mode", G_CALLBACK (count_notify), &notified);
    g_object_set (item, "selection-mode", FALSE, NULL);
    g_assert_cmpuint (notified, ==, 0);
    g_object_set (item, "selection-mode", TRUE, NULL);
    g_object_set (item, "selection-mode", TRUE, NULL);
    g_assert_cmpuint (notified, ==, 1);

    gtk_window_destroy (GTK_WINDOW (window));
}

/* The star names what pressing it does, the accessible name and the tooltip
 * alike: the name set when the button was built says "Pin". */
static void
test_item_star_named (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    GtkWidget *window = gtk_window_new ();
    GtkWidget *item = g_paste_ui_item_new (client, settings, GTK_WINDOW (window), 0);
    GPasteUiItem *row = G_PASTE_UI_ITEM (item);

    g_assert_no_error (error);
    /* In the window: a widget has an accessible context once it is rooted. */
    gtk_window_set_child (GTK_WINDOW (window), item);
    gtk_window_present (GTK_WINDOW (window));
    g_paste_ui_item_set_favourited (row, TRUE);
    gtk_test_accessible_assert_property (GTK_ACCESSIBLE (row->favourite), GTK_ACCESSIBLE_PROPERTY_LABEL, "Unpin");
    g_paste_ui_item_set_favourited (row, FALSE);
    gtk_test_accessible_assert_property (GTK_ACCESSIBLE (row->favourite), GTK_ACCESSIBLE_PROPERTY_LABEL, "Pin");

    gtk_window_destroy (GTK_WINDOW (window));
}

static gboolean
sent (gconstpointer arg1 G_GNUC_UNUSED,
      gconstpointer arg2 G_GNUC_UNUSED)
{
    return sent_name != NULL;
}

static gboolean
widget_mapped (gconstpointer widget,
               gconstpointer arg G_GNUC_UNUSED)
{
    return gtk_widget_get_mapped ((GtkWidget *) widget);
}

/* A row handed to another item shows nothing of the one it showed, and offers
 * nothing on the one it now answers for until that item is read. */
static void
test_item_rebound_blank (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    GtkWidget *window = gtk_window_new ();
    g_autoptr (GtkWidget) item = g_object_ref_sink (g_paste_ui_item_new (client, settings, GTK_WINDOW (window), 0));
    GPasteUiItem *row = G_PASTE_UI_ITEM (item);

    g_assert_no_error (error);
    g_paste_ui_item_set_text (row, "previous");
    g_paste_ui_item_set_index (row, (guint64) -1);
    g_assert_cmpstr (gtk_inscription_get_text (row->label), ==, "");

    g_paste_ui_item_set_uuid (row, "00000000-0000-4000-8000-000000000001");
    g_assert_false (g_action_get_enabled (G_ACTION (item_action (row, "delete"))));
    g_assert_false (g_action_get_enabled (G_ACTION (item_action (row, "pin"))));

    gtk_window_destroy (GTK_WINDOW (window));
}

/* A sidebar listing "work" and "work_backup", in a window that hosts dialogs. */
static GPasteUiPanel *
panel_window (GPasteClient *client,
              GtkWindow   **window)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    GtkWidget *search = gtk_search_entry_new ();
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

    *window = GTK_WINDOW (adw_window_new ());

    GtkWidget *panel = g_paste_ui_panel_new (client, settings, *window, GTK_SEARCH_ENTRY (search));
    GPasteUiPanel *self = G_PASTE_UI_PANEL (panel);

    gtk_box_append (GTK_BOX (box), search);
    gtk_box_append (GTK_BOX (box), panel);
    adw_window_set_content (ADW_WINDOW (*window), box);
    gtk_window_present (*window);
    g_paste_ui_panel_add_history (self, "work", NULL, FALSE);
    g_paste_ui_panel_add_history (self, "work_backup", NULL, FALSE);

    return self;
}

/* Switch or Create says a name the daemon would refuse as it is typed and
 * never sends it, and keeps what was typed until the switch went through. */
static void
test_panel_switch_name (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    GtkWindow *window;
    GPasteUiPanel *self = panel_window (client, &window);
    GtkEditable *entry = GTK_EDITABLE (self->switch_entry);

    g_assert_no_error (error);
    gtk_editable_set_text (entry, "notes/2026");
    g_assert_false (gtk_widget_get_sensitive (GTK_WIDGET (self->jump_button)));
    g_assert_true (gtk_widget_has_css_class (GTK_WIDGET (entry), "error"));
    g_paste_ui_panel_do_switch (self);
    g_assert_null (sent_callback);

    gtk_editable_set_text (entry, "notes");
    g_assert_true (gtk_widget_get_sensitive (GTK_WIDGET (self->jump_button)));
    g_assert_false (gtk_widget_has_css_class (GTK_WIDGET (entry), "error"));
    g_paste_ui_panel_do_switch (self);
    g_assert_cmpstr (sent_name, ==, "notes");
    g_assert_cmpstr (gtk_editable_get_text (entry), ==, "notes");

    answer_error = g_error_new_literal (G_PASTE_ERROR, G_PASTE_ERROR_FAILED, "refused");
    g_test_expect_message ("GPaste", G_LOG_LEVEL_WARNING, "Could not switch history: refused");
    answer (client);
    g_test_assert_expected_messages ();
    g_assert_cmpstr (gtk_editable_get_text (entry), ==, "notes");

    g_paste_ui_panel_do_switch (self);
    answer (client);
    g_assert_cmpstr (gtk_editable_get_text (entry), ==, "");

    g_paste_ui_panel_do_switch (self);
    g_assert_cmpstr (sent_name, ==, G_PASTE_DEFAULT_HISTORY);
    answer (client);
    gtk_window_destroy (window);
}

/* The Back Up dialog's name is one the daemon would take before Back Up can be
 * chosen, and Enter chooses it. */
static void
test_panel_backup_name (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    GtkWindow *window;
    GPasteUiPanel *self = panel_window (client, &window);

    g_assert_no_error (error);
    g_signal_emit_by_name (self->sidebar, "setup-menu", history_find (self->histories, "work")->data);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (self), "panel.backup-history", NULL));

    AdwAlertDialog *dialog = ADW_ALERT_DIALOG (adw_window_get_visible_dialog (ADW_WINDOW (window)));
    GtkWidget *entry = adw_alert_dialog_get_extra_child (dialog);

    /* "work_backup" is offered, and listed already. */
    g_assert_cmpstr (gtk_editable_get_text (GTK_EDITABLE (entry)), ==, "work_backup");
    g_assert_false (adw_alert_dialog_get_response_enabled (dialog, "backup"));
    g_assert_true (gtk_widget_has_css_class (entry, "error"));
    gtk_editable_set_text (GTK_EDITABLE (entry), "old/backup");
    g_assert_false (adw_alert_dialog_get_response_enabled (dialog, "backup"));
    gtk_editable_set_text (GTK_EDITABLE (entry), "");
    g_assert_false (adw_alert_dialog_get_response_enabled (dialog, "backup"));
    g_assert_false (gtk_widget_has_css_class (entry, "error"));
    gtk_editable_set_text (GTK_EDITABLE (entry), "work_2026");
    g_assert_true (adw_alert_dialog_get_response_enabled (dialog, "backup"));
    g_assert_false (gtk_widget_has_css_class (entry, "error"));

    /* The name has the focus, and Enter there backs up, as the text the entry
     * delegates to has it: Back Up answers once the dialog has closed. */
    g_paste_test_bus_wait_until (widget_mapped, entry, NULL);
    g_assert_true (gtk_widget_has_focus (GTK_WIDGET (gtk_editable_get_delegate (GTK_EDITABLE (entry)))));
    g_clear_pointer (&sent_name, g_free);
    g_signal_emit_by_name (gtk_editable_get_delegate (GTK_EDITABLE (entry)), "activate");
    g_paste_test_bus_wait_until (sent, NULL, NULL);
    g_assert_cmpstr (sent_name, ==, "work_2026");
    answer (client);
    gtk_window_destroy (window);
}

/* A window going with the Back Up dialog up takes the dialog and what it holds
 * with it. */
static void
test_panel_backup_window_gone (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    GtkWindow *window;
    GPasteUiPanel *self = panel_window (client, &window);

    g_assert_no_error (error);
    g_signal_emit_by_name (self->sidebar, "setup-menu", history_find (self->histories, "work")->data);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (self), "panel.backup-history", NULL));

    gpointer dialog = adw_window_get_visible_dialog (ADW_WINDOW (window));

    g_assert_nonnull (dialog);
    g_object_add_weak_pointer (dialog, &dialog);
    gtk_window_destroy (window);
    g_assert_null (dialog);
}

int
main (int argc, char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DISPLAY);
    have_display = g_paste_test_env_has_display () && gtk_init_check ();
    if (have_display)
        adw_init ();
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/ui/panel-history/revealed", test_revealed);
    g_test_add_func ("/ui/panel-history/delete-key", test_delete_key);
    g_test_add_func ("/ui/item/selection-mode-notify", test_item_selection_mode_notify);
    g_test_add_func ("/ui/item/star-named", test_item_star_named);
    g_test_add_func ("/ui/item/rebound-blank", test_item_rebound_blank);
    g_test_add_func ("/ui/panel/switch-name", test_panel_switch_name);
    g_test_add_func ("/ui/panel/backup-name", test_panel_backup_name);
    g_test_add_func ("/ui/panel/backup-window-gone", test_panel_backup_window_gone);
    return g_paste_test_env_run ();
}
