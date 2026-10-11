// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>
#include <gpaste-test-env.h>

#include <dlfcn.h>

/* Access the dialog lifecycle without starting the asynchronous daemon client. */
#include <gpaste-ui-window.c>

static void
wait_for_close (void)
{
    g_paste_test_bus_pump (350);
}

static gboolean have_display;

static void
shortcuts_changed (gconstpointer user_data)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GPasteUiWindow) window = g_object_ref_sink (g_object_new (G_PASTE_TYPE_UI_WINDOW, NULL));
    g_paste_settings_set_keybindings_enabled (window->settings, TRUE);
    gtk_window_present (GTK_WINDOW (window));
    on_show_help_overlay (NULL, NULL, window);
    g_assert_nonnull (window->shortcuts);
    g_autoptr (AdwDialog) original = g_object_ref (window->shortcuts);

    if (GPOINTER_TO_INT (user_data))
        g_paste_settings_set_keybindings_enabled (window->settings, FALSE);
    else
        g_paste_settings_set_launch_ui (window->settings, "<Control>F12");

    g_assert_cmpuint (window->shortcuts_source, !=, 0);
    wait_for_close ();
    g_assert_null (window->shortcuts);
    g_assert_cmpuint (window->shortcuts_source, ==, 0);

    on_show_help_overlay (NULL, NULL, window);
    g_assert_nonnull (window->shortcuts);
    g_assert_true (window->shortcuts != original);
    g_paste_settings_set_keybindings_enabled (window->settings, TRUE);
    if (GPOINTER_TO_INT (user_data))
    {
        g_assert_cmpuint (window->shortcuts_source, !=, 0);
        wait_for_close ();
        g_assert_null (window->shortcuts);
    }
    gtk_window_destroy (GTK_WINDOW (window));
}

static GtkWidget *
find_widget_of_type (GtkWidget *root,
                     GType      type)
{
    if (G_TYPE_CHECK_INSTANCE_TYPE (root, type))
        return root;

    for (GtkWidget *child = gtk_widget_get_first_child (root); child; child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = find_widget_of_type (child, type);

        if (found)
            return found;
    }

    return NULL;
}

/* Icon-only buttons have a name as well as a tooltip: New's on the split button
 * itself, which its inner button is labelled by, and the custom separator's. */
static void
icon_buttons_named (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    /* In a window: a widget has an accessible context once it is rooted. */
    GtkWidget *header_window = gtk_window_new ();
    GtkWidget *header = g_paste_ui_header_new ();
    GtkWidget *split = find_widget_of_type (header, ADW_TYPE_SPLIT_BUTTON);

    gtk_window_set_child (GTK_WINDOW (header_window), header);
    gtk_window_present (GTK_WINDOW (header_window));
    g_assert_nonnull (split);
    gtk_test_accessible_assert_property (GTK_ACCESSIBLE (split), GTK_ACCESSIBLE_PROPERTY_LABEL, "New Item");
    gtk_window_destroy (GTK_WINDOW (header_window));

    /* The merge bar alone, which the window builds once it has a client. */
    g_autoptr (GPasteUiWindow) window = g_object_ref_sink (g_object_new (G_PASTE_TYPE_UI_WINDOW, NULL));
    GtkWidget *bar_window = gtk_window_new ();

    gtk_window_set_child (GTK_WINDOW (bar_window), build_merge_bar (window));
    gtk_window_present (GTK_WINDOW (bar_window));

    GtkWidget *custom_merge = gtk_widget_get_next_sibling (window->merge_entry);

    g_assert_true (GTK_IS_BUTTON (custom_merge));
    gtk_test_accessible_assert_property (GTK_ACCESSIBLE (custom_merge), GTK_ACCESSIBLE_PROPERTY_LABEL, "Merge With This Separator");
    gtk_window_destroy (GTK_WINDOW (bar_window));
    gtk_window_destroy (GTK_WINDOW (window));
}

/* The sections of the last shortcuts dialog built: the dialog has no getter
 * for them. */
static GPtrArray *added_sections;

void
adw_shortcuts_dialog_add (AdwShortcutsDialog  *self,
                          AdwShortcutsSection *section)
{
    typedef void (*Add) (AdwShortcutsDialog *, AdwShortcutsSection *);
    Add original = (Add) dlsym (RTLD_NEXT, "adw_shortcuts_dialog_add");

    g_assert_nonnull (original);
    if (added_sections)
        g_ptr_array_add (added_sections, g_object_ref (section));
    original (self, section);
}

static gboolean
dialog_lists (guint           keyval,
              GdkModifierType modifiers)
{
    for (guint s = 0; s < added_sections->len; ++s)
    {
        GListModel *section = added_sections->pdata[s];

        for (guint i = 0; i < g_list_model_get_n_items (section); ++i)
        {
            g_autoptr (AdwShortcutsItem) item = g_list_model_get_item (section, i);
            g_auto (GStrv) accelerators = g_strsplit (adw_shortcuts_item_get_accelerator (item), " ", 0);

            for (GStrv a = accelerators; *a; ++a)
            {
                guint parsed_keyval;
                GdkModifierType parsed_modifiers;

                if (gtk_accelerator_parse (*a, &parsed_keyval, &parsed_modifiers) && parsed_keyval == keyval && parsed_modifiers == modifiers)
                    return TRUE;
            }
        }
    }

    return FALSE;
}

/* The keys the history list opens an item's menu on
 * (G_PASTE_UI_HISTORY_CONTEXT_MENU_ACCELERATORS, which it reads too) are the
 * window's own, and listed with them, every one. */
static void
context_menu_keys_listed (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    added_sections = g_ptr_array_new_with_free_func (g_object_unref);
    g_autoptr (AdwDialog) dialog = g_object_ref_sink (ADW_DIALOG (g_paste_ui_shortcuts_window_new (settings)));

    g_auto (GStrv) accelerators = g_strsplit (G_PASTE_UI_HISTORY_CONTEXT_MENU_ACCELERATORS, " ", -1);

    for (GStrv accelerator = accelerators; *accelerator; ++accelerator)
    {
        guint keyval;
        GdkModifierType modifiers;

        g_assert_true (gtk_accelerator_parse (*accelerator, &keyval, &modifiers));
        g_assert_true (dialog_lists (keyval, modifiers));
    }
    g_assert_true (dialog_lists (GDK_KEY_Menu, 0));
    g_assert_true (dialog_lists (GDK_KEY_F10, GDK_SHIFT_MASK));
    g_clear_pointer (&added_sections, g_ptr_array_unref);
}

int
main (int argc, char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DISPLAY);
    have_display = g_paste_test_env_has_display () && gtk_init_check ();
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    if (have_display)
        adw_init ();
    g_test_add_data_func ("/ui/shortcuts/master-switch", GINT_TO_POINTER (TRUE), shortcuts_changed);
    g_test_add_data_func ("/ui/shortcuts/accelerator", GINT_TO_POINTER (FALSE), shortcuts_changed);
    g_test_add_func ("/ui/icon-buttons-named", icon_buttons_named);
    g_test_add_func ("/ui/shortcuts/context-menu-keys", context_menu_keys_listed);
    return g_paste_test_env_run ();
}
