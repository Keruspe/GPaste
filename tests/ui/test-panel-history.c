// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>
#include <gpaste-test-env.h>

/* Reach the row the sidebar draws the history on, and the revealer on it. */
#include <gpaste-ui-panel-history.c>
/* And the row an item is drawn on, for its star. */
#include <gpaste-ui-item.c>
#include <gpaste-3/gpaste-settings.h>

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
    return g_paste_test_env_run ();
}
