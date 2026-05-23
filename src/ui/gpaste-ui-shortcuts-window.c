// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-ui-shortcuts-window.h>

/**
 * g_paste_ui_shortcuts_window_new:
 * @settings: a #GPasteSettings instance
 *
 * Create a new #AdwShortcutsDialog for GPaste
 *
 * Returns: a newly allocated #AdwShortcutsDialog
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_shortcuts_window_new (const GPasteSettings *settings)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);

    AdwDialog *self = adw_shortcuts_dialog_new ();
    AdwShortcutsSection *section = adw_shortcuts_section_new (_("General"));

    adw_shortcuts_section_add (section, adw_shortcuts_item_new (_("Delete the active item from history"),          g_paste_settings_get_pop (settings)));
    adw_shortcuts_section_add (section, adw_shortcuts_item_new (_("Launch the graphical tool"),                    g_paste_settings_get_launch_ui (settings)));
    adw_shortcuts_section_add (section, adw_shortcuts_item_new (_("Mark the active item as being a password"),     g_paste_settings_get_make_password (settings)));
    adw_shortcuts_section_add (section, adw_shortcuts_item_new (_("Display the history"),                          g_paste_settings_get_show_history (settings)));
    adw_shortcuts_section_add (section, adw_shortcuts_item_new (_("Sync the clipboard to the primary selection"),  g_paste_settings_get_sync_clipboard_to_primary (settings)));
    adw_shortcuts_section_add (section, adw_shortcuts_item_new (_("Sync the primary selection to the clipboard"),  g_paste_settings_get_sync_primary_to_clipboard (settings)));
    adw_shortcuts_section_add (section, adw_shortcuts_item_new (_("Upload the active item to a pastebin service"), g_paste_settings_get_upload (settings)));

    adw_shortcuts_dialog_add (ADW_SHORTCUTS_DIALOG (self), section);

    return GTK_WIDGET (self);
}
