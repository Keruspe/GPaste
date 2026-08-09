// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-pages.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-group.h>

static void
on_storage_migration_activated (AdwButtonRow *row G_GNUC_UNUSED,
                                gpointer      user_data G_GNUC_UNUSED)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);

    if (!client)
    {
        g_warning ("Could not connect to the daemon to migrate: %s", error->message);
        return;
    }

    /* Force the migration gate open and re-execute the daemon (through the shared
     * helper): it flushes the history, re-runs the migration (on its next start
     * when standalone, in place when hosted in gnome-shell) and reloads the
     * newly-chosen backend, rather than racing it by migrating from here. */
    if (!g_paste_util_trigger_storage_migration (client, &error))
        g_warning ("Could not trigger the storage migration: %s", error->message);
}

static void
on_change_passphrase_activated (AdwButtonRow *row G_GNUC_UNUSED,
                                gpointer      user_data G_GNUC_UNUSED)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);

    if (!client)
    {
        g_warning ("Could not connect to the daemon to change the passphrase: %s", error->message);
        return;
    }

    /* The daemon owns the prompts (it holds the current passphrase, and knows
     * how to raise them in its host), so ask it rather than doing any of it
     * here; the passphrases never travel over the bus. */
    g_paste_client_change_passphrase_sync (client, &error);

    if (error)
        g_warning ("Could not change the passphrase: %s", error->message);
}

static void
update_passphrase_sensitivity (GPasteSettings *settings,
                               GParamSpec     *pspec G_GNUC_UNUSED,
                               gpointer        user_data)
{
    gtk_widget_set_sensitive (GTK_WIDGET (user_data),
                              g_paste_storage_is_encrypted (g_paste_settings_get_storage_backend (settings)));
}

/**
 * g_paste_gtk_preferences_history_settings_page_new:
 * @settings: a #GPasteSettings instance
 *
 * Build the preferences page
 *
 * Returns: (transfer full): a newly allocated #AdwPreferencesPage
 */
AdwPreferencesPage *
g_paste_gtk_preferences_history_settings_page_new (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    AdwPreferencesPage *self = ADW_PREFERENCES_PAGE (g_object_new (ADW_TYPE_PREFERENCES_PAGE,
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
    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

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
    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    group = g_paste_gtk_preferences_group_new (_("Display settings"));
    g_paste_gtk_preferences_group_add_range_setting (group,
                                                     _("Max element size when displaying"),
                                                     G_PASTE_ELEMENT_SIZE_SETTING,
                                                     0, 511, 5,
                                                     settings);
    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    group = g_paste_gtk_preferences_group_new (_("Storage"));
    adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (group),
                                           _("Choose how the history is stored on disk (plain or encrypted)."));
    g_paste_gtk_preferences_group_add_button (group,
                                              _("Change storage backend…"),
                                              G_CALLBACK (on_storage_migration_activated),
                                              NULL);

    /* Only an encrypted history has a passphrase to change, so the row stays
     * visible (it is how the feature is discovered) but insensitive otherwise.
     * Bound to the setting rather than read once: the button above can change
     * the backend from this very window. */
    AdwButtonRow *passphrase = g_paste_gtk_preferences_group_add_button (group,
                                                                          _("Change passphrase…"),
                                                                          G_CALLBACK (on_change_passphrase_activated),
                                                                          NULL);

    update_passphrase_sensitivity (settings, NULL, passphrase);
    g_signal_connect_object (settings, "notify::" G_PASTE_STORAGE_BACKEND_SETTING,
                             G_CALLBACK (update_passphrase_sensitivity), passphrase, 0);

    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    return self;
}
