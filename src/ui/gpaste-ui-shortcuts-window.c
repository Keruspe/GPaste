// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-ui-shortcuts-window.h>

/**
 * g_paste_ui_shortcuts_window_new:
 * @settings: a #GPasteSettings instance
 *
 * Create a new #AdwShortcutsDialog for GPaste
 *
 * The shortcuts are always listed -- they are still the chords GPaste would
 * grab -- under a section title that says whether anything is listening for
 * them. The dialog reflects the settings as of the time it is built, so it is
 * built anew every time, and closed when what it shows changes under it.
 *
 * Returns: (transfer none): a newly created #AdwShortcutsDialog, holding the
 *          floating reference its caller sinks
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_shortcuts_window_new (const GPasteSettings *settings)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);

    AdwDialog *self = adw_shortcuts_dialog_new ();
    gboolean enabled = g_paste_settings_get_keybindings_enabled (settings);

    /* The notice the keybindings-enabled master switch being off deserves is
     * the title of the one section there is. A section of its own would have to
     * be an empty one -- a section carries one or more items, and what an
     * itemless one renders as is nobody's contract -- and an item cannot carry
     * a notice either: with an empty accelerator it renders a "No Shortcut"
     * chip next to the text, and with none at all it is not rendered.
     * The warning sign is part of the translated string: where it belongs is
     * the translator's call, and an RTL locale wants it at the other end. */
    AdwShortcutsSection *section = adw_shortcuts_section_new (enabled
        ? _("General")
        : _("⚠ Global shortcuts are disabled — GPaste is not listening for any of the shortcuts below"));

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
