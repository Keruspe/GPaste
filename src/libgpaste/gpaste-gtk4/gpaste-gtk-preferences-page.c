// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-page.h>

G_PASTE_GTK_DEFINE_TYPE (PreferencesPage, preferences_page, ADW_TYPE_PREFERENCES_PAGE)

/**
 * g_paste_gtk_preferences_page_add_group:
 * @self: a #GPasteGtkPreferencesPage instance
 * @group: a #GPasteGtkPreferencesGroup instance
 *
 * Add @group to the page. The group's rows are bound to their settings, so no
 * extra tracking is needed.
 */
G_PASTE_VISIBLE void
g_paste_gtk_preferences_page_add_group (GPasteGtkPreferencesPage  *self,
                                        GPasteGtkPreferencesGroup *group)
{
    g_return_if_fail (G_PASTE_IS_GTK_PREFERENCES_PAGE (self));
    g_return_if_fail (G_PASTE_IS_GTK_PREFERENCES_GROUP (group));

    adw_preferences_page_add (ADW_PREFERENCES_PAGE (self), ADW_PREFERENCES_GROUP (group));
}

static void
g_paste_gtk_preferences_page_class_init (GPasteGtkPreferencesPageClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_gtk_preferences_page_init (GPasteGtkPreferencesPage *self G_GNUC_UNUSED)
{
}
