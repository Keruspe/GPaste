// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-behaviour-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-history-settings-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-images-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-shortcuts-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-widget.h>

struct _GPasteGtkPreferencesWidget
{
    AdwBin parent_instance;
};

G_PASTE_GTK_DEFINE_TYPE (PreferencesWidget, preferences_widget, ADW_TYPE_BIN)

static void
g_paste_gtk_preferences_widget_class_init (GPasteGtkPreferencesWidgetClass *klass G_GNUC_UNUSED)
{
}

static void
add_page (AdwViewStack *s,
          GtkWidget    *page)
{
    AdwPreferencesPage *p = ADW_PREFERENCES_PAGE (page);
    AdwViewStackPage *asp = adw_view_stack_add_titled (s, page, adw_preferences_page_get_name (p), adw_preferences_page_get_title (p));

    adw_view_stack_page_set_icon_name (asp, adw_preferences_page_get_icon_name (p));
}

static void
g_paste_gtk_preferences_widget_init (GPasteGtkPreferencesWidget *self)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    GtkWidget *stack = adw_view_stack_new ();
    AdwViewStack *s = ADW_VIEW_STACK (stack);

    add_page (s, g_paste_gtk_preferences_behaviour_page_new (settings));
    add_page (s, g_paste_gtk_preferences_history_settings_page_new (settings));
    add_page (s, g_paste_gtk_preferences_images_page_new (settings));
    add_page (s, g_paste_gtk_preferences_shortcuts_page_new (settings));

    /* Wide: a switcher above the content. Narrow: hide it and reveal a bottom
     * AdwViewSwitcherBar instead, the standard adaptive pattern. */
    GtkWidget *top_switcher = GTK_WIDGET (g_object_new (ADW_TYPE_VIEW_SWITCHER,
                                                        "stack", stack,
                                                        "policy", ADW_VIEW_SWITCHER_POLICY_WIDE,
                                                        NULL));
    GtkWidget *bottom_bar = adw_view_switcher_bar_new ();
    adw_view_switcher_bar_set_stack (ADW_VIEW_SWITCHER_BAR (bottom_bar), s);

    gtk_widget_set_vexpand (stack, TRUE);

    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append (GTK_BOX (box), top_switcher);
    gtk_box_append (GTK_BOX (box), stack);
    gtk_box_append (GTK_BOX (box), bottom_bar);

    GtkWidget *bin = adw_breakpoint_bin_new ();
    adw_breakpoint_bin_set_child (ADW_BREAKPOINT_BIN (bin), box);

    AdwBreakpoint *breakpoint = adw_breakpoint_new (adw_breakpoint_condition_parse ("max-width: 500px"));
    adw_breakpoint_add_setters (breakpoint,
                                G_OBJECT (top_switcher), "visible", FALSE,
                                G_OBJECT (bottom_bar), "reveal", TRUE,
                                NULL);
    adw_breakpoint_bin_add_breakpoint (ADW_BREAKPOINT_BIN (bin), breakpoint);

    adw_bin_set_child (ADW_BIN (self), bin);
}

/**
 * g_paste_gtk_preferences_widget_new:
 *
 * Create a new instance of #GPasteGtkPreferencesWidget
 *
 * Returns: (nullable): a newly allocated #GPasteGtkPreferencesWidget
 *                      free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_gtk_preferences_widget_new (void)
{
    return GTK_WIDGET (g_object_new (G_PASTE_TYPE_GTK_PREFERENCES_WIDGET, NULL));
}
