// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-history-settings-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-group.h>

struct _GPasteGtkPreferencesHistorySettingsPage
{
    GPasteGtkPreferencesPage parent_instance;
};

G_PASTE_GTK_DEFINE_TYPE (PreferencesHistorySettingsPage, preferences_history_settings_page, G_PASTE_TYPE_GTK_PREFERENCES_PAGE)

static void
on_storage_migration_activated (AdwButtonRow *row G_GNUC_UNUSED,
                                gpointer      user_data G_GNUC_UNUSED)
{
    g_autoptr (GError) error = NULL;

    /* The dialog needs gtk_init/Adw and a nested main loop, so run it out of
     * process in the dedicated helper rather than inside the preferences. */
    g_autoptr (GSubprocess) proc = g_paste_util_spawn_storage_migration (&error);

    if (!proc)
        g_warning ("Could not start the storage migration: %s", error->message);
}

static void
g_paste_gtk_preferences_history_settings_page_class_init (GPasteGtkPreferencesHistorySettingsPageClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_gtk_preferences_history_settings_page_init (GPasteGtkPreferencesHistorySettingsPage *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_gtk_preferences_history_settings_page_new:
 * @settings: a #GPasteSettings instance
 *
 * Create a new instance of #GPasteGtkPreferencesHistorySettingsPage
 *
 * Returns: (nullable): a newly allocated #GPasteGtkPreferencesHistorySettingsPage
 *                      free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_gtk_preferences_history_settings_page_new (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    GPasteGtkPreferencesHistorySettingsPage *self = G_PASTE_GTK_PREFERENCES_HISTORY_SETTINGS_PAGE (g_object_new (G_PASTE_TYPE_GTK_PREFERENCES_HISTORY_SETTINGS_PAGE,
                                                                                                                 "name", "history-settings",
                                                                                                                 "title", _("History settings"),
                                                                                                                 "icon-name", "preferences-other",
                                                                                                                 NULL));

    GPasteGtkPreferencesGroup *group = g_paste_gtk_preferences_group_new (_("Resources limits"));
    g_paste_gtk_preferences_group_add_range_setting (group,
                                                     _("Max history size"),
                                                     G_PASTE_MAX_HISTORY_SIZE_SETTING,
                                                     5, 65535, 5,
                                                     settings);
    g_paste_gtk_preferences_group_add_range_setting (group,
                                                     _("Max memory usage (MB)"),
                                                     G_PASTE_MAX_MEMORY_USAGE_SETTING,
                                                     5, 16383, 5,
                                                     settings);
    g_paste_gtk_preferences_page_add_group (G_PASTE_GTK_PREFERENCES_PAGE (self), group);

    group = g_paste_gtk_preferences_group_new (_("Text limits"));
    g_paste_gtk_preferences_group_add_range_setting (group,
                                                     _("Min text item length"),
                                                     G_PASTE_MIN_TEXT_ITEM_SIZE_SETTING,
                                                     1, 65535, 1,
                                                     settings);
    g_paste_gtk_preferences_group_add_range_setting (group,
                                                     _("Max text item length"),
                                                     G_PASTE_MAX_TEXT_ITEM_SIZE_SETTING,
                                                     1, 2147483647, 1,
                                                     settings);
    g_paste_gtk_preferences_page_add_group (G_PASTE_GTK_PREFERENCES_PAGE (self), group);

    group = g_paste_gtk_preferences_group_new (_("Display settings"));
    g_paste_gtk_preferences_group_add_range_setting (group,
                                                     _("Max element size when displaying"),
                                                     G_PASTE_ELEMENT_SIZE_SETTING,
                                                     0, 511, 5,
                                                     settings);
    g_paste_gtk_preferences_page_add_group (G_PASTE_GTK_PREFERENCES_PAGE (self), group);

    group = g_paste_gtk_preferences_group_new (_("Storage"));
    adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (group),
                                           _("Choose how the history is stored on disk (plain or encrypted)."));
    g_paste_gtk_preferences_group_add_button (group,
                                              _("Change storage backend…"),
                                              G_CALLBACK (on_storage_migration_activated),
                                              NULL);
    g_paste_gtk_preferences_page_add_group (G_PASTE_GTK_PREFERENCES_PAGE (self), group);

    return GTK_WIDGET (self);
}
