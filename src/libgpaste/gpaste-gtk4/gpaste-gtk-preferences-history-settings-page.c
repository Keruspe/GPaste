/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk4/gpaste-gtk-preferences-history-settings-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-group.h>

struct _GPasteGtkPreferencesHistorySettingsPage
{
    GPasteGtkPreferencesPage parent_instance;
};

typedef struct
{
    GPasteGtkPreferencesManager *manager;

    GtkSpinButton               *max_history_size_button;
    GtkSpinButton               *max_memory_usage_button;

    GtkSpinButton               *min_text_item_size_button;
    GtkSpinButton               *max_text_item_size_button;

    GtkSpinButton               *element_size_button;
    GtkSpinButton               *max_displayed_history_size_button;
} GPasteGtkPreferencesHistorySettingsPagePrivate;

G_PASTE_GTK_DEFINE_TYPE_WITH_PRIVATE (PreferencesHistorySettingsPage, preferences_history_settings_page, G_PASTE_TYPE_GTK_PREFERENCES_PAGE)

static void
g_paste_gtk_preferences_history_settings_page_setting_changed (GPasteGtkPreferencesPage *self,
                                                        GPasteSettings           *settings,
                                                        const gchar              *key)
{
    GPasteGtkPreferencesHistorySettingsPagePrivate *priv = g_paste_gtk_preferences_history_settings_page_get_instance_private (G_PASTE_GTK_PREFERENCES_HISTORY_SETTINGS_PAGE (self));

    if (g_paste_str_equal (key, G_PASTE_ELEMENT_SIZE_SETTING))
        gtk_spin_button_set_value (priv->element_size_button, g_paste_settings_get_element_size (settings));
    else if (g_paste_str_equal (key, G_PASTE_MAX_DISPLAYED_HISTORY_SIZE_SETTING))
        gtk_spin_button_set_value (priv->max_displayed_history_size_button, g_paste_settings_get_max_displayed_history_size (settings));
    else if (g_paste_str_equal (key, G_PASTE_MAX_HISTORY_SIZE_SETTING))
        gtk_spin_button_set_value (priv->max_history_size_button, g_paste_settings_get_max_history_size (settings));
    else if (g_paste_str_equal (key, G_PASTE_MAX_MEMORY_USAGE_SETTING))
        gtk_spin_button_set_value (priv->max_memory_usage_button, g_paste_settings_get_max_memory_usage (settings));
    else if (g_paste_str_equal (key, G_PASTE_MAX_TEXT_ITEM_SIZE_SETTING))
        gtk_spin_button_set_value (priv->max_text_item_size_button, g_paste_settings_get_max_text_item_size (settings));
    else if (g_paste_str_equal (key, G_PASTE_MIN_TEXT_ITEM_SIZE_SETTING))
        gtk_spin_button_set_value (priv->min_text_item_size_button, g_paste_settings_get_min_text_item_size (settings));
}

static void
g_paste_gtk_preferences_history_settings_page_dispose (GObject *object)
{
    GPasteGtkPreferencesHistorySettingsPagePrivate *priv = g_paste_gtk_preferences_history_settings_page_get_instance_private (G_PASTE_GTK_PREFERENCES_HISTORY_SETTINGS_PAGE (object));

    if (priv->manager) /* first dispose call */
    {
        g_paste_gtk_preferences_manager_deregister (priv->manager, G_PASTE_GTK_PREFERENCES_PAGE (object));
        g_clear_object (&priv->manager);
    }

    G_OBJECT_CLASS (g_paste_gtk_preferences_history_settings_page_parent_class)->dispose (object);
}

static void
g_paste_gtk_preferences_history_settings_page_class_init (GPasteGtkPreferencesHistorySettingsPageClass *klass)
{
    G_PASTE_GTK_PREFERENCES_PAGE_CLASS (klass)->setting_changed = g_paste_gtk_preferences_history_settings_page_setting_changed;

    G_OBJECT_CLASS (klass)->dispose = g_paste_gtk_preferences_history_settings_page_dispose;
}

static void
g_paste_gtk_preferences_history_settings_page_init (GPasteGtkPreferencesHistorySettingsPage *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_gtk_preferences_history_settings_page_new:
 * @manager: a #GPasteGtkPreferencesManager instance
 *
 * Create a new instance of #GPasteGtkPreferencesHistorySettingsPage
 *
 * Returns: (nullable): a newly allocated #GPasteGtkPreferencesHistorySettingsPage
 *                      free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_gtk_preferences_history_settings_page_new (GPasteGtkPreferencesManager *manager)
{
    g_return_val_if_fail (G_PASTE_IS_GTK_PREFERENCES_MANAGER (manager), NULL);

    GPasteGtkPreferencesHistorySettingsPage *self = G_PASTE_GTK_PREFERENCES_HISTORY_SETTINGS_PAGE (g_object_new (G_PASTE_TYPE_GTK_PREFERENCES_HISTORY_SETTINGS_PAGE,
                                                                                                                 "name", "history-settings",
                                                                                                                 "title", _("History settings"),
                                                                                                                 "icon-name", "preferences-other",
                                                                                                                 NULL));
    GPasteGtkPreferencesHistorySettingsPagePrivate *priv = g_paste_gtk_preferences_history_settings_page_get_instance_private (self);
    GPasteSettings *settings = g_paste_gtk_preferences_manager_get_settings (manager);
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE (self);

    priv->manager = g_object_ref (manager);

    g_paste_gtk_preferences_manager_register (manager, G_PASTE_GTK_PREFERENCES_PAGE (self));

    GPasteGtkPreferencesGroup *group = g_paste_gtk_preferences_group_new (_("Resources limits"));
    priv->max_history_size_button = g_paste_gtk_preferences_group_add_range_setting (group,
                                                                                     _("Max history size"),
                                                                                     (gdouble) g_paste_settings_get_max_history_size (settings),
                                                                                     100, 65535, 5,
                                                                                     g_paste_settings_set_max_history_size,
                                                                                     g_paste_settings_reset_max_history_size,
                                                                                     settings);
    priv->max_memory_usage_button = g_paste_gtk_preferences_group_add_range_setting (group,
                                                                                     _("Max memory usage (MB)"),
                                                                                     (gdouble) g_paste_settings_get_max_memory_usage (settings),
                                                                                     5, 16383, 5,
                                                                                     g_paste_settings_set_max_memory_usage,
                                                                                     g_paste_settings_reset_max_memory_usage,
                                                                                     settings);
    adw_preferences_page_add (page, ADW_PREFERENCES_GROUP (group));

    group = g_paste_gtk_preferences_group_new (_("Text limits"));
    priv->min_text_item_size_button = g_paste_gtk_preferences_group_add_range_setting (group,
                                                                                       _("Min text item length"),
                                                                                       (gdouble) g_paste_settings_get_min_text_item_size (settings),
                                                                                       1, 65535, 1,
                                                                                       g_paste_settings_set_min_text_item_size,
                                                                                       g_paste_settings_reset_min_text_item_size,
                                                                                       settings);
    priv->max_text_item_size_button = g_paste_gtk_preferences_group_add_range_setting (group,
                                                                                       _("Max text item length"),
                                                                                       (gdouble) g_paste_settings_get_max_text_item_size (settings),
                                                                                       1, (gdouble) G_MAXUINT64, 1,
                                                                                       g_paste_settings_set_max_text_item_size,
                                                                                       g_paste_settings_reset_max_text_item_size,
                                                                                       settings);
    adw_preferences_page_add (page, ADW_PREFERENCES_GROUP (group));

    group = g_paste_gtk_preferences_group_new (_("Display settings"));
    priv->element_size_button = g_paste_gtk_preferences_group_add_range_setting (group,
                                                                                 _("Max element size when displaying"),
                                                                                 (gdouble) g_paste_settings_get_element_size (settings),
                                                                                 0, 511, 5,
                                                                                 g_paste_settings_set_element_size,
                                                                                 g_paste_settings_reset_element_size,
                                                                                 settings);
    priv->max_displayed_history_size_button = g_paste_gtk_preferences_group_add_range_setting (group,
                                                                                               _("Max displayed history size"),
                                                                                               (gdouble) g_paste_settings_get_max_displayed_history_size (settings),
                                                                                               10, 255, 5,
                                                                                               g_paste_settings_set_max_displayed_history_size,
                                                                                               g_paste_settings_reset_max_displayed_history_size,
                                                                                               settings);
    adw_preferences_page_add (page, ADW_PREFERENCES_GROUP (group));

    return GTK_WIDGET (self);
}
