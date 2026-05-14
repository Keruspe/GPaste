/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk4/gpaste-gtk-preferences-images-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-group.h>

struct _GPasteGtkPreferencesImagesPage
{
    GPasteGtkPreferencesPage parent_instance;
};

typedef struct
{
    GPasteGtkPreferencesManager *manager;

    AdwSwitchRow                *images_support_switch;
    AdwSwitchRow                *images_preview_switch;
    AdwSpinRow                  *images_preview_size_button;
} GPasteGtkPreferencesImagesPagePrivate;

G_PASTE_GTK_DEFINE_TYPE_WITH_PRIVATE (PreferencesImagesPage, preferences_images_page, G_PASTE_TYPE_GTK_PREFERENCES_PAGE)

static void
g_paste_gtk_preferences_images_page_setting_changed (GPasteGtkPreferencesPage *self,
                                                     GPasteSettings           *settings,
                                                     const gchar              *key)
{
    GPasteGtkPreferencesImagesPagePrivate *priv = g_paste_gtk_preferences_images_page_get_instance_private (G_PASTE_GTK_PREFERENCES_IMAGES_PAGE (self));

    if (g_paste_str_equal (key, G_PASTE_IMAGES_SUPPORT_SETTING))
        adw_switch_row_set_active (priv->images_support_switch, g_paste_settings_get_images_support (settings));
    else if (g_paste_str_equal (key, G_PASTE_IMAGES_PREVIEW_SETTING))
        adw_switch_row_set_active (priv->images_preview_switch, g_paste_settings_get_images_preview (settings));
    else if (g_paste_str_equal (key, G_PASTE_IMAGES_PREVIEW_SIZE_SETTING))
        adw_spin_row_set_value (priv->images_preview_size_button, g_paste_settings_get_images_preview_size (settings));
}

static void
g_paste_gtk_preferences_images_page_dispose (GObject *object)
{
    GPasteGtkPreferencesImagesPagePrivate *priv = g_paste_gtk_preferences_images_page_get_instance_private (G_PASTE_GTK_PREFERENCES_IMAGES_PAGE (object));

    if (priv->manager) /* first dispose call */
    {
        g_paste_gtk_preferences_manager_deregister (priv->manager, G_PASTE_GTK_PREFERENCES_PAGE (object));
        g_clear_object (&priv->manager);
    }

    G_OBJECT_CLASS (g_paste_gtk_preferences_images_page_parent_class)->dispose (object);
}

static void
g_paste_gtk_preferences_images_page_class_init (GPasteGtkPreferencesImagesPageClass *klass)
{
    G_PASTE_GTK_PREFERENCES_PAGE_CLASS (klass)->setting_changed = g_paste_gtk_preferences_images_page_setting_changed;

    G_OBJECT_CLASS (klass)->dispose = g_paste_gtk_preferences_images_page_dispose;
}

static void
g_paste_gtk_preferences_images_page_init (GPasteGtkPreferencesImagesPage *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_gtk_preferences_images_page_new:
 * @manager: a #GPasteGtkPreferencesManager instance
 *
 * Create a new instance of #GPasteGtkPreferencesImagesPage
 *
 * Returns: (nullable): a newly allocated #GPasteGtkPreferencesImagesPage
 *                      free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_gtk_preferences_images_page_new (GPasteGtkPreferencesManager *manager)
{
    g_return_val_if_fail (G_PASTE_IS_GTK_PREFERENCES_MANAGER (manager), NULL);

    GPasteGtkPreferencesImagesPage *self = G_PASTE_GTK_PREFERENCES_IMAGES_PAGE (g_object_new (G_PASTE_TYPE_GTK_PREFERENCES_IMAGES_PAGE,
                                                                                               "name", "images",
                                                                                               "title", _("Images settings"),
                                                                                               "icon-name", "image-x-generic",
                                                                                               NULL));
    GPasteGtkPreferencesImagesPagePrivate *priv = g_paste_gtk_preferences_images_page_get_instance_private (self);
    GPasteSettings *settings = g_paste_gtk_preferences_manager_get_settings (manager);
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE (self);

    priv->manager = g_object_ref (manager);

    g_paste_gtk_preferences_manager_register (manager, G_PASTE_GTK_PREFERENCES_PAGE (self));

    GPasteGtkPreferencesGroup *group = g_paste_gtk_preferences_group_new (_("Images settings"));
    priv->images_support_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                     _("Images support"),
                                                                                     g_paste_settings_get_images_support (settings),
                                                                                     g_paste_settings_set_images_support,
                                                                                     g_paste_settings_reset_images_support,
                                                                                     settings);
    priv->images_preview_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                     _("Image previews"),
                                                                                     g_paste_settings_get_images_preview (settings),
                                                                                     g_paste_settings_set_images_preview,
                                                                                     g_paste_settings_reset_images_preview,
                                                                                     settings);
    priv->images_preview_size_button = g_paste_gtk_preferences_group_add_range_setting (group,
                                                                                        _("Preview size"),
                                                                                        (gdouble) g_paste_settings_get_images_preview_size (settings),
                                                                                        50, 400, 10,
                                                                                        g_paste_settings_set_images_preview_size,
                                                                                        g_paste_settings_reset_images_preview_size,
                                                                                        settings);
    adw_preferences_page_add (page, ADW_PREFERENCES_GROUP (group));

    return GTK_WIDGET (self);
}
