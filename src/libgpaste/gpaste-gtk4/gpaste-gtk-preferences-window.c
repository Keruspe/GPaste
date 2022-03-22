/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk4/gpaste-gtk-preferences-behaviour-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-history-settings-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-shortcuts-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-window.h>

struct _GPasteGtkPreferencesWindow
{
    AdwPreferencesWindow parent_instance;
};

G_PASTE_GTK_DEFINE_TYPE (PreferencesWindow, preferences_window, ADW_TYPE_PREFERENCES_WINDOW)

static void
g_paste_gtk_preferences_window_class_init (GPasteGtkPreferencesWindowClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_gtk_preferences_window_init (GPasteGtkPreferencesWindow *self)
{
    AdwPreferencesWindow *win = ADW_PREFERENCES_WINDOW (self);
    g_autoptr (GPasteGtkPreferencesManager) manager = g_paste_gtk_preferences_manager_new ();
    
    adw_preferences_window_add (win, ADW_PREFERENCES_PAGE (g_paste_gtk_preferences_behaviour_page_new (manager)));
    adw_preferences_window_add (win, ADW_PREFERENCES_PAGE (g_paste_gtk_preferences_history_settings_page_new (manager)));
    adw_preferences_window_add (win, ADW_PREFERENCES_PAGE (g_paste_gtk_preferences_shortcuts_page_new (manager)));
}

/**
 * g_paste_gtk_preferences_window_new:
 *
 * Create a new instance of #GPasteGtkPreferencesWindow
 *
 * Returns: (nullable): a newly allocated #GPasteGtkPreferencesWindow
 *                      free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWindow *
g_paste_gtk_preferences_window_new (void)
{
    return GTK_WINDOW (g_object_new (G_PASTE_TYPE_GTK_PREFERENCES_WINDOW, NULL));
}
