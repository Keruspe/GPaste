/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk4/gpaste-gtk-preferences-behaviour-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-history-settings-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-shortcuts-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-widget.h>

#include <adwaita.h>

struct _GPasteGtkPreferencesWidget
{
    GtkBox parent_instance;
};

G_PASTE_DEFINE_TYPE (GtkPreferencesWidget, gtk_preferences_widget, GTK_TYPE_BOX)

static void
g_paste_gtk_preferences_widget_class_init (GPasteGtkPreferencesWidgetClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_gtk_preferences_widget_init (GPasteGtkPreferencesWidget *self)
{
    GtkBox *box = GTK_BOX (self);
    g_autoptr (GPasteGtkPreferencesManager) manager = g_paste_gtk_preferences_manager_new ();
    GtkWidget *stack = adw_view_stack_new ();
    AdwViewStack *s = ADW_VIEW_STACK (stack);
    GtkWidget *switcher = GTK_WIDGET (g_object_new (ADW_TYPE_VIEW_SWITCHER, "stack", stack, NULL));
    
    adw_view_stack_add (s, g_paste_gtk_preferences_behaviour_page_new (manager));
    adw_view_stack_add (s, g_paste_gtk_preferences_history_settings_page_new (manager));
    adw_view_stack_add (s, g_paste_gtk_preferences_shortcuts_page_new (manager));

    gtk_box_append (box, switcher);
    gtk_box_append (box, stack);
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
    return GTK_WIDGET (g_object_new (G_PASTE_TYPE_GTK_PREFERENCES_WIDGET, "orientation", GTK_ORIENTATION_VERTICAL, NULL));
}
