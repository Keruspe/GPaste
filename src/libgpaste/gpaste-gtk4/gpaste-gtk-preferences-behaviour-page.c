// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-behaviour-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-group.h>

struct _GPasteGtkPreferencesBehaviourPage
{
    GPasteGtkPreferencesPage parent_instance;
};

typedef struct
{
    GPasteGtkPreferencesManager *manager;

    AdwSwitchRow                *track_changes_switch;
    AdwSwitchRow                *close_on_select_switch;
    AdwSwitchRow                *open_centered_switch;
    AdwSwitchRow                *save_history_switch;

    AdwSwitchRow                *extension_enabled_switch;
    AdwSwitchRow                *track_extension_state_switch;

    AdwSwitchRow                *primary_to_history_switch;
    AdwSwitchRow                *synchronize_clipboards_switch;

    AdwSwitchRow                *growing_lines_switch;
    AdwSwitchRow                *trim_items_switch;
} GPasteGtkPreferencesBehaviourPagePrivate;

G_PASTE_GTK_DEFINE_TYPE_WITH_PRIVATE (PreferencesBehaviourPage, preferences_behaviour_page, G_PASTE_TYPE_GTK_PREFERENCES_PAGE)

static void
g_paste_gtk_preferences_behaviour_page_setting_changed (GPasteGtkPreferencesPage *self,
                                                        GPasteSettings           *settings,
                                                        const gchar              *key)
{
    GPasteGtkPreferencesBehaviourPagePrivate *priv = g_paste_gtk_preferences_behaviour_page_get_instance_private (G_PASTE_GTK_PREFERENCES_BEHAVIOUR_PAGE (self));

    if (g_paste_str_equal (key, G_PASTE_CLOSE_ON_SELECT_SETTING))
        adw_switch_row_set_active (priv->close_on_select_switch, g_paste_settings_get_close_on_select (settings));
    else if (g_paste_str_equal (key, G_PASTE_OPEN_CENTERED_SETTING))
        adw_switch_row_set_active (priv->open_centered_switch, g_paste_settings_get_open_centered (settings));
    else if (g_paste_str_equal (key, G_PASTE_GROWING_LINES_SETTING))
        adw_switch_row_set_active (priv->growing_lines_switch, g_paste_settings_get_growing_lines (settings));
    else if (g_paste_str_equal (key, G_PASTE_PRIMARY_TO_HISTORY_SETTING ))
        adw_switch_row_set_active (priv->primary_to_history_switch, g_paste_settings_get_primary_to_history (settings));
    else if (g_paste_str_equal (key, G_PASTE_SAVE_HISTORY_SETTING))
        adw_switch_row_set_active (priv->save_history_switch, g_paste_settings_get_save_history (settings));
    else if (g_paste_str_equal (key, G_PASTE_SYNCHRONIZE_CLIPBOARDS_SETTING))
        adw_switch_row_set_active (priv->synchronize_clipboards_switch, g_paste_settings_get_synchronize_clipboards (settings));
    else if (g_paste_str_equal (key, G_PASTE_TRACK_CHANGES_SETTING))
        adw_switch_row_set_active (priv->track_changes_switch, g_paste_settings_get_track_changes (settings));
    else if (g_paste_str_equal (key, G_PASTE_TRIM_ITEMS_SETTING))
        adw_switch_row_set_active (priv->trim_items_switch, g_paste_settings_get_trim_items (settings));
    else if (g_paste_util_has_gnome_shell ())
    {
        if (g_paste_str_equal (key, G_PASTE_EXTENSION_ENABLED_SETTING))
            adw_switch_row_set_active (priv->extension_enabled_switch, g_paste_settings_get_extension_enabled (settings));
        else if (g_paste_str_equal (key, G_PASTE_TRACK_EXTENSION_STATE_SETTING))
            adw_switch_row_set_active (priv->track_extension_state_switch, g_paste_settings_get_track_extension_state (settings));
    }
}

static void
g_paste_gtk_preferences_behaviour_page_dispose (GObject *object)
{
    GPasteGtkPreferencesBehaviourPagePrivate *priv = g_paste_gtk_preferences_behaviour_page_get_instance_private (G_PASTE_GTK_PREFERENCES_BEHAVIOUR_PAGE (object));

    if (priv->manager) /* first dispose call */
    {
        g_paste_gtk_preferences_manager_deregister (priv->manager, G_PASTE_GTK_PREFERENCES_PAGE (object));
        g_clear_object (&priv->manager);
    }

    G_OBJECT_CLASS (g_paste_gtk_preferences_behaviour_page_parent_class)->dispose (object);
}

static void
g_paste_gtk_preferences_behaviour_page_class_init (GPasteGtkPreferencesBehaviourPageClass *klass)
{
    G_PASTE_GTK_PREFERENCES_PAGE_CLASS (klass)->setting_changed = g_paste_gtk_preferences_behaviour_page_setting_changed;

    G_OBJECT_CLASS (klass)->dispose = g_paste_gtk_preferences_behaviour_page_dispose;
}

static void
g_paste_gtk_preferences_behaviour_page_init (GPasteGtkPreferencesBehaviourPage *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_gtk_preferences_behaviour_page_new:
 * @manager: a #GPasteGtkPreferencesManager instance
 *
 * Create a new instance of #GPasteGtkPreferencesBehaviourPage
 *
 * Returns: (nullable): a newly allocated #GPasteGtkPreferencesBehaviourPage
 *                      free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_gtk_preferences_behaviour_page_new (GPasteGtkPreferencesManager *manager)
{
    g_return_val_if_fail (G_PASTE_IS_GTK_PREFERENCES_MANAGER (manager), NULL);

    GPasteGtkPreferencesBehaviourPage *self = G_PASTE_GTK_PREFERENCES_BEHAVIOUR_PAGE (g_object_new (G_PASTE_TYPE_GTK_PREFERENCES_BEHAVIOUR_PAGE,
                                                                                                    "name", "behaviour",
                                                                                                    "title", _("General behaviour"),
                                                                                                    "icon-name", "preferences-system",
                                                                                                    NULL));
    GPasteGtkPreferencesBehaviourPagePrivate *priv = g_paste_gtk_preferences_behaviour_page_get_instance_private (self);
    GPasteSettings *settings = g_paste_gtk_preferences_manager_get_settings (manager);
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE (self);

    priv->manager = g_object_ref (manager);

    g_paste_gtk_preferences_manager_register (manager, G_PASTE_GTK_PREFERENCES_PAGE (self));

    GPasteGtkPreferencesGroup *group = g_paste_gtk_preferences_group_new (_("General behaviour"));
    priv->track_changes_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                    _("Track clipboard changes"),
                                                                                    g_paste_settings_get_track_changes (settings),
                                                                                    g_paste_settings_set_track_changes,
                                                                                    g_paste_settings_reset_track_changes,
                                                                                    settings);
    priv->close_on_select_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                      _("Close UI on select"),
                                                                                      g_paste_settings_get_close_on_select (settings),
                                                                                      g_paste_settings_set_close_on_select,
                                                                                      g_paste_settings_reset_close_on_select,
                                                                                      settings);
    priv->open_centered_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                    _("Open the UI window centered"),
                                                                                    g_paste_settings_get_open_centered (settings),
                                                                                    g_paste_settings_set_open_centered,
                                                                                    g_paste_settings_reset_open_centered,
                                                                                    settings);
    priv->save_history_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                   _("Save history"),
                                                                                   g_paste_settings_get_save_history (settings),
                                                                                   g_paste_settings_set_save_history,
                                                                                   g_paste_settings_reset_save_history,
                                                                                   settings);
    adw_preferences_page_add (page, ADW_PREFERENCES_GROUP (group));

    if (g_paste_util_has_gnome_shell ())
    {
        group = g_paste_gtk_preferences_group_new ("GNOME shell");
        priv->extension_enabled_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                            _("Enable the gnome-shell extension"),
                                                                                            g_paste_settings_get_extension_enabled (settings),
                                                                                            g_paste_settings_set_extension_enabled,
                                                                                            NULL,
                                                                                            settings);
        priv->track_extension_state_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                                _("Sync the daemon state with the extension's one"),
                                                                                                g_paste_settings_get_track_extension_state (settings),
                                                                                                g_paste_settings_set_track_extension_state,
                                                                                                g_paste_settings_reset_track_extension_state,
                                                                                                settings);
        adw_action_row_set_subtitle (ADW_ACTION_ROW (priv->track_extension_state_switch),
                                     _("When enabled, the daemon automatically starts or stops tracking clipboard changes to match the GNOME Shell extension's enabled state"));
        adw_preferences_page_add (page, ADW_PREFERENCES_GROUP (group));
    }

    group = g_paste_gtk_preferences_group_new (_("Clipboards synchronization"));
    priv->primary_to_history_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                         _("Primary selection affects history"),
                                                                                         g_paste_settings_get_primary_to_history (settings),
                                                                                         g_paste_settings_set_primary_to_history,
                                                                                         g_paste_settings_reset_primary_to_history,
                                                                                         settings);
    priv->synchronize_clipboards_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                             _("Synchronize clipboard with primary selection"),
                                                                                             g_paste_settings_get_synchronize_clipboards (settings),
                                                                                             g_paste_settings_set_synchronize_clipboards,
                                                                                             g_paste_settings_reset_synchronize_clipboards,
                                                                                             settings);
    adw_preferences_page_add (page, ADW_PREFERENCES_GROUP (group));

    group = g_paste_gtk_preferences_group_new (_("Optional features"));
    priv->trim_items_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                  _("Trim items"),
                                                                                 g_paste_settings_get_trim_items (settings),
                                                                                 g_paste_settings_set_trim_items,
                                                                                 g_paste_settings_reset_trim_items,
                                                                                 settings);
    priv->growing_lines_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                     _("Detect growing lines"),
                                                                                    g_paste_settings_get_growing_lines (settings),
                                                                                    g_paste_settings_set_growing_lines,
                                                                                    g_paste_settings_reset_growing_lines,
                                                                                    settings);
    adw_action_row_set_subtitle (ADW_ACTION_ROW (priv->growing_lines_switch),
                                 _("When enabled, if a new clipboard entry starts with the previous one, the previous entry is replaced instead of creating a new one"));
    adw_preferences_page_add (page, ADW_PREFERENCES_GROUP (group));

    return GTK_WIDGET (self);
}
