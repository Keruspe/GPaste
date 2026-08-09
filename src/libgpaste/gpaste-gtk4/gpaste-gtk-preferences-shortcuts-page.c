// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-group.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-pages.h>

/**
 * g_paste_gtk_preferences_shortcuts_page_new:
 * @settings: a #GPasteSettings instance
 *
 * Build the preferences page
 *
 * Returns: (transfer full): a newly allocated #AdwPreferencesPage
 */
AdwPreferencesPage *
g_paste_gtk_preferences_shortcuts_page_new (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    AdwPreferencesPage *self = ADW_PREFERENCES_PAGE (g_object_new (ADW_TYPE_PREFERENCES_PAGE,
                                                                   "name", "shortcuts",
                                                                   "title", _("Keyboard shortcuts"),
                                                                   "icon-name", "preferences-desktop-keyboard-shortcuts",
                                                                   NULL));

    GPasteGtkPreferencesGroup *group = g_paste_gtk_preferences_group_new (_("History access"));
    /* translators: Keyboard shortcut to launch the graphical tool */
    g_paste_gtk_preferences_group_add_shortcut_setting (group,
                                                        _("Launch the graphical tool"),
                                                        G_PASTE_LAUNCH_UI_SETTING,
                                                        settings);
    /* translators: Keyboard shortcut to display the history */
    g_paste_gtk_preferences_group_add_shortcut_setting (group,
                                                        _("Display the history"),
                                                        G_PASTE_SHOW_HISTORY_SETTING,
                                                        settings);
    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    group = g_paste_gtk_preferences_group_new (_("Active element manipulation"));
    /* translators: Keyboard shortcut to mark the active item as being a password */
    g_paste_gtk_preferences_group_add_shortcut_setting (group,
                                                        _("Mark the active item as being a password"),
                                                        G_PASTE_MAKE_PASSWORD_SETTING,
                                                        settings);
    /* translators: Keyboard shortcut to upload the active item from history to a pastebin service */
    g_paste_gtk_preferences_group_add_shortcut_setting (group,
                                                        _("Upload the active item to a pastebin service"),
                                                        G_PASTE_UPLOAD_SETTING,
                                                        settings);
    /* translators: Keyboard shortcut to delete the active item from history */
    g_paste_gtk_preferences_group_add_shortcut_setting (group,
                                                        _("Delete the active item from history"),
                                                        G_PASTE_POP_SETTING,
                                                        settings);
    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    group = g_paste_gtk_preferences_group_new (_("Clipboards synchronization"));
    /* translators: Keyboard shortcut to sync the clipboard to the primary selection */
    g_paste_gtk_preferences_group_add_shortcut_setting (group,
                                                        _("Sync the clipboard to the primary selection"),
                                                        G_PASTE_SYNC_CLIPBOARD_TO_PRIMARY_SETTING,
                                                        settings);
    /* translators: Keyboard shortcut to sync the primary selection to the clipboard */
    g_paste_gtk_preferences_group_add_shortcut_setting (group,
                                                        _("Sync the primary selection to the clipboard"),
                                                        G_PASTE_SYNC_PRIMARY_TO_CLIPBOARD_SETTING,
                                                        settings);
    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    return self;
}
