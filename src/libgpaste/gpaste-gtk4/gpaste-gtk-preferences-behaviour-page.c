// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-behaviour-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-group.h>

struct _GPasteGtkPreferencesBehaviourPage
{
    GPasteGtkPreferencesPage parent_instance;
};

G_PASTE_GTK_DEFINE_TYPE (PreferencesBehaviourPage, preferences_behaviour_page, G_PASTE_TYPE_GTK_PREFERENCES_PAGE)

static void
g_paste_gtk_preferences_behaviour_page_class_init (GPasteGtkPreferencesBehaviourPageClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_gtk_preferences_behaviour_page_init (GPasteGtkPreferencesBehaviourPage *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_gtk_preferences_behaviour_page_new:
 * @settings: a #GPasteSettings instance
 *
 * Create a new instance of #GPasteGtkPreferencesBehaviourPage
 *
 * Returns: (nullable): a newly allocated #GPasteGtkPreferencesBehaviourPage
 *                      free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_gtk_preferences_behaviour_page_new (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    GPasteGtkPreferencesBehaviourPage *self = G_PASTE_GTK_PREFERENCES_BEHAVIOUR_PAGE (g_object_new (G_PASTE_TYPE_GTK_PREFERENCES_BEHAVIOUR_PAGE,
                                                                                                    "name", "behaviour",
                                                                                                    "title", _("General behaviour"),
                                                                                                    "icon-name", "preferences-system",
                                                                                                    NULL));

    GPasteGtkPreferencesGroup *group = g_paste_gtk_preferences_group_new (_("General behaviour"));
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Track clipboard changes"),
                                                       G_PASTE_TRACK_CHANGES_SETTING,
                                                       settings);
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Close UI on select"),
                                                       G_PASTE_CLOSE_ON_SELECT_SETTING,
                                                       settings);
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Open the UI window centered"),
                                                       G_PASTE_OPEN_CENTERED_SETTING,
                                                       settings);
    g_paste_gtk_preferences_page_add_group (G_PASTE_GTK_PREFERENCES_PAGE (self), group);

    if (g_paste_util_has_gnome_shell ())
    {
        group = g_paste_gtk_preferences_group_new ("GNOME shell");

        /* "extension-enabled" is derived from the shell schema, not a plain key,
         * so it has no default to reset to: bind it without a reset suffix. */
        AdwSwitchRow *extension_enabled_switch = ADW_SWITCH_ROW (adw_switch_row_new ());
        adw_preferences_row_set_title (ADW_PREFERENCES_ROW (extension_enabled_switch), _("Enable the gnome-shell extension"));
        g_object_bind_property (settings, G_PASTE_EXTENSION_ENABLED_SETTING, extension_enabled_switch, "active",
                                G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
        adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), GTK_WIDGET (extension_enabled_switch));

        AdwSwitchRow *track_extension_state_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                                       _("Sync the daemon state with the extension's one"),
                                                                                                       G_PASTE_TRACK_EXTENSION_STATE_SETTING,
                                                                                                       settings);
        adw_action_row_set_subtitle (ADW_ACTION_ROW (track_extension_state_switch),
                                     _("When enabled, the daemon automatically starts or stops tracking clipboard changes to match the GNOME Shell extension's enabled state"));
        g_paste_gtk_preferences_page_add_group (G_PASTE_GTK_PREFERENCES_PAGE (self), group);
    }

    group = g_paste_gtk_preferences_group_new (_("Clipboards synchronization"));
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Primary selection affects history"),
                                                       G_PASTE_PRIMARY_TO_HISTORY_SETTING,
                                                       settings);
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Synchronize clipboard with primary selection"),
                                                       G_PASTE_SYNCHRONIZE_CLIPBOARDS_SETTING,
                                                       settings);
    g_paste_gtk_preferences_page_add_group (G_PASTE_GTK_PREFERENCES_PAGE (self), group);

    group = g_paste_gtk_preferences_group_new (_("Optional features"));
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Trim items"),
                                                       G_PASTE_TRIM_ITEMS_SETTING,
                                                       settings);
    AdwSwitchRow *growing_lines_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                           _("Detect growing lines"),
                                                                                           G_PASTE_GROWING_LINES_SETTING,
                                                                                           settings);
    adw_action_row_set_subtitle (ADW_ACTION_ROW (growing_lines_switch),
                                 _("When enabled, if a new clipboard entry starts with the previous one, the previous entry is replaced instead of creating a new one"));
    g_paste_gtk_preferences_page_add_group (G_PASTE_GTK_PREFERENCES_PAGE (self), group);

    return GTK_WIDGET (self);
}
