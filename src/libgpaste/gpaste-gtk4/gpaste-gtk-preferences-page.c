/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk4/gpaste-gtk-preferences-page.h>

G_PASTE_GTK_DEFINE_TYPE (PreferencesPage, preferences_page, ADW_TYPE_PREFERENCES_PAGE)

/**
 * g_paste_gtk_preferences_page_setting_changed:
 * @self: a #GPasteGtkPreferencesPage instance
 * @settings: a #GPasteSettings instance
 * @key: the settings key that just changed
 *
 * Apply changes related to the update of one setting
 */
G_PASTE_VISIBLE void
g_paste_gtk_preferences_page_setting_changed (GPasteGtkPreferencesPage *self,
                                              GPasteSettings           *settings,
                                              const gchar              *key)
{
    g_return_if_fail (G_PASTE_IS_GTK_PREFERENCES_PAGE (self));
    g_return_if_fail (G_PASTE_IS_SETTINGS (settings));
    g_return_if_fail (key);

    GPasteGtkPreferencesPageClass *klass = G_PASTE_GTK_PREFERENCES_PAGE_GET_CLASS (self);

    if (klass->setting_changed)
        klass->setting_changed (self, settings, key);
}

static void
g_paste_gtk_preferences_page_class_init (GPasteGtkPreferencesPageClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_gtk_preferences_page_init (GPasteGtkPreferencesPage *self G_GNUC_UNUSED)
{
}
