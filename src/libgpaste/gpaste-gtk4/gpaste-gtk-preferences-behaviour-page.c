// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-pages.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-group.h>

/**
 * g_paste_gtk_preferences_behaviour_page_new:
 * @settings: a #GPasteSettings instance
 *
 * Build the preferences page
 *
 * Returns: (transfer full): a newly allocated #AdwPreferencesPage
 */
AdwPreferencesPage *
g_paste_gtk_preferences_behaviour_page_new (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    AdwPreferencesPage *self = ADW_PREFERENCES_PAGE (g_object_new (ADW_TYPE_PREFERENCES_PAGE,
                                                                   "name", "behaviour",
                                                                   "title", _("General Behaviour"),
                                                                   "icon-name", "preferences-system",
                                                                   NULL));

    GPasteGtkPreferencesGroup *group = g_paste_gtk_preferences_group_new (_("General Behaviour"));
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Track Clipboard Changes"),
                                                       G_PASTE_TRACK_CHANGES_SETTING,
                                                       settings);
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Close UI on Select"),
                                                       G_PASTE_CLOSE_ON_SELECT_SETTING,
                                                       settings);
    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    if (g_paste_util_has_gnome_shell ())
    {
        group = g_paste_gtk_preferences_group_new ("GNOME Shell");

        /* "extension-enabled" is derived from the shell schema, not a plain key,
         * so it has no default to reset to: bind it without a reset suffix. */
        AdwSwitchRow *extension_enabled_switch = ADW_SWITCH_ROW (adw_switch_row_new ());
        adw_preferences_row_set_title (ADW_PREFERENCES_ROW (extension_enabled_switch), _("Enable the GNOME Shell Extension"));
        g_object_bind_property (settings, G_PASTE_EXTENSION_ENABLED_SETTING, extension_enabled_switch, "active",
                                G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
        adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), GTK_WIDGET (extension_enabled_switch));

        AdwSwitchRow *track_extension_state_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                                       _("Match the Daemon State to the Extension's"),
                                                                                                       G_PASTE_TRACK_EXTENSION_STATE_SETTING,
                                                                                                       settings);
        adw_action_row_set_subtitle (ADW_ACTION_ROW (track_extension_state_switch),
                                     _("When enabled, the daemon automatically starts or stops tracking clipboard changes to match the GNOME Shell extension's enabled state"));

        AdwSwitchRow *experimental_meta_daemon_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                                          _("Use the Experimental In-Shell Daemon"),
                                                                                                          G_PASTE_EXPERIMENTAL_META_DAEMON_SETTING,
                                                                                                          settings);
        adw_action_row_set_subtitle (ADW_ACTION_ROW (experimental_meta_daemon_switch),
                                     _("Experimental: run the daemon inside GNOME Shell (mutter clipboard) instead of the standalone one. Takes effect after the extension restarts"));

        adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));
    }

    group = g_paste_gtk_preferences_group_new (_("Clipboard Synchronization"));
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Primary Selection Affects History"),
                                                       G_PASTE_PRIMARY_TO_HISTORY_SETTING,
                                                       settings);
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Synchronize Clipboard With Primary Selection"),
                                                       G_PASTE_SYNCHRONIZE_CLIPBOARDS_SETTING,
                                                       settings);
    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    group = g_paste_gtk_preferences_group_new (_("Optional Features"));
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Trim Items"),
                                                       G_PASTE_TRIM_ITEMS_SETTING,
                                                       settings);
    AdwSwitchRow *growing_lines_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                           _("Detect Growing Lines"),
                                                                                           G_PASTE_GROWING_LINES_SETTING,
                                                                                           settings);
    adw_action_row_set_subtitle (ADW_ACTION_ROW (growing_lines_switch),
                                 _("When enabled, if a new clipboard entry starts with the previous one, the previous entry is replaced instead of creating a new one"));
    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    return self;
}
