// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-images-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-group.h>

struct _GPasteGtkPreferencesImagesPage
{
    GPasteGtkPreferencesPage parent_instance;
};

G_PASTE_GTK_DEFINE_TYPE (PreferencesImagesPage, preferences_images_page, G_PASTE_TYPE_GTK_PREFERENCES_PAGE)

static void
g_paste_gtk_preferences_images_page_class_init (GPasteGtkPreferencesImagesPageClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_gtk_preferences_images_page_init (GPasteGtkPreferencesImagesPage *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_gtk_preferences_images_page_new:
 * @settings: a #GPasteSettings instance
 *
 * Create a new instance of #GPasteGtkPreferencesImagesPage
 *
 * Returns: (nullable): a newly allocated #GPasteGtkPreferencesImagesPage
 *                      free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_gtk_preferences_images_page_new (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    GPasteGtkPreferencesImagesPage *self = G_PASTE_GTK_PREFERENCES_IMAGES_PAGE (g_object_new (G_PASTE_TYPE_GTK_PREFERENCES_IMAGES_PAGE,
                                                                                               "name", "images",
                                                                                               "title", _("Images settings"),
                                                                                               "icon-name", "image-x-generic",
                                                                                               NULL));

    GPasteGtkPreferencesGroup *group = g_paste_gtk_preferences_group_new (_("Images settings"));
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Images support"),
                                                       G_PASTE_IMAGES_SUPPORT_SETTING,
                                                       settings);
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Image previews"),
                                                       G_PASTE_IMAGES_PREVIEW_SETTING,
                                                       settings);
    g_paste_gtk_preferences_group_add_range_setting (group,
                                                     _("Preview size"),
                                                     G_PASTE_IMAGES_PREVIEW_SIZE_SETTING,
                                                     50, 400, 10,
                                                     settings);
    g_paste_gtk_preferences_page_add_group (G_PASTE_GTK_PREFERENCES_PAGE (self), group);

    return GTK_WIDGET (self);
}
