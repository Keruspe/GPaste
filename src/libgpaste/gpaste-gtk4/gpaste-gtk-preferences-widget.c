/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk4/gpaste-gtk-preferences-behaviour-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-history-settings-page.h>
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
    AdwBin *bin = ADW_BIN (self);
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    GtkBox *b = GTK_BOX (box);
    g_autoptr (GPasteGtkPreferencesManager) manager = g_paste_gtk_preferences_manager_new ();
    GtkWidget *stack = adw_view_stack_new ();
    AdwViewStack *s = ADW_VIEW_STACK (stack);
    GtkWidget *switcher = GTK_WIDGET (g_object_new (ADW_TYPE_VIEW_SWITCHER, "stack", stack, "policy", ADW_VIEW_SWITCHER_POLICY_WIDE, NULL));
    
    add_page (s, g_paste_gtk_preferences_behaviour_page_new (manager));
    add_page (s, g_paste_gtk_preferences_history_settings_page_new (manager));
    add_page (s, g_paste_gtk_preferences_shortcuts_page_new (manager));

    gtk_box_append (b, switcher);
    gtk_box_append (b, stack);

    adw_bin_set_child (bin, box);
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
