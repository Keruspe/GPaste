// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-manager.h>
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

/**
 * g_paste_gtk_preferences_page_register:
 * @self: a #GPasteGtkPreferencesPage instance
 * @manager: a #GPasteGtkPreferencesManager instance
 *
 * Register a page that is done building with its manager
 *
 * Every page's constructor ends with this, and none registers before: the
 * manager hands setting_changed () every "changed" from the moment of the
 * register, and a page still being built takes one with its row pointers unset
 * — straight into a setter of its own, which, unlike the entry path, has no
 * tolerance for a %NULL row. Nothing but building a page not iterating the main
 * loop keeps one from landing in between, which is not a page's to guarantee.
 * The page comes back so that a constructor can end with the register itself:
 * one place holds the rule, rather than a copy of it per page.
 *
 * Returns: (transfer none): @self, as the #GtkWidget a page constructor returns
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_gtk_preferences_page_register (GPasteGtkPreferencesPage    *self,
                                       GPasteGtkPreferencesManager *manager)
{
    g_return_val_if_fail (G_PASTE_IS_GTK_PREFERENCES_PAGE (self), NULL);
    g_return_val_if_fail (G_PASTE_IS_GTK_PREFERENCES_MANAGER (manager), NULL);

    g_paste_gtk_preferences_manager_register (manager, self);

    return GTK_WIDGET (self);
}

static void
g_paste_gtk_preferences_page_class_init (GPasteGtkPreferencesPageClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_gtk_preferences_page_init (GPasteGtkPreferencesPage *self G_GNUC_UNUSED)
{
}
