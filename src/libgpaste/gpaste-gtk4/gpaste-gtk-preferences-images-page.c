// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-pages.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-group.h>

/**
 * g_paste_gtk_preferences_images_page_new:
 * @settings: a #GPasteSettings instance
 *
 * Build the preferences page
 *
 * Returns: (transfer full): a newly allocated #AdwPreferencesPage
 */
AdwPreferencesPage *
g_paste_gtk_preferences_images_page_new (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    AdwPreferencesPage *self = ADW_PREFERENCES_PAGE (g_object_new (ADW_TYPE_PREFERENCES_PAGE,
                                                                   "name", "images",
                                                                   "title", _("Image settings"),
                                                                   "icon-name", "image-x-generic",
                                                                   NULL));

    GPasteGtkPreferencesGroup *group = g_paste_gtk_preferences_group_new (_("Image settings"));
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Image support"),
                                                       G_PASTE_IMAGES_SUPPORT_SETTING,
                                                       settings);
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Image previews"),
                                                       G_PASTE_IMAGES_PREVIEW_SETTING,
                                                       settings);
    g_paste_gtk_preferences_group_add_range_setting (group,
                                                     _("Preview size"),
                                                     G_PASTE_IMAGES_PREVIEW_SIZE_SETTING,
                                                     16, 200, 10,
                                                     settings);
    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    return self;
}
