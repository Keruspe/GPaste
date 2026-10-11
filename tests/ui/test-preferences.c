// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* The preferences' rows, on a private Xvfb: what a range row, a text row and a
 * shortcut row hand their setting, and what each leaves behind. */

#include <gpaste-test-env.h>

#include "../../src/libgpaste/gpaste-gtk4/gpaste-gtk-preferences-group.c"
#include "../../src/libgpaste/gpaste-gtk4/gpaste-gtk-shortcut-row.c"

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

    return g_paste_test_env_run ();
}
