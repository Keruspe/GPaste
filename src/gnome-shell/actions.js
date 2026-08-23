// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {PopupMenuItem, PopupSeparatorMenuItem} from 'resource:///org/gnome/shell/ui/popupMenu.js';

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';

import GPaste from 'gi://GPaste?version=3';

/**
 * Add what the menu can do beyond listing the history, under the history it
 * lists.
 *
 * Plain menu items rather than a row of buttons: an item is focusable and the
 * menu walks between them on its own, where the row that held the buttons was
 * non-reactive, so no key event ever bubbled through it and every arrow had to
 * be answered by hand from the menu actor.
 *
 * About is not among them. A panel menu is not where an application's about
 * dialog belongs, and it cost a slot that Preferences -- which is what a user
 * actually reaches for there -- had none of.
 *
 * @param {PopupMenu} menu - the indicator's menu
 * @param {Extension} extension - the extension, which owns the preferences
 * @param {GPaste.Client} client - a connected client
 * @param {GPaste.Settings} settings - the settings the confirmation reads
 * @returns {object} the three items, by name
 */
export function addGPasteFooter(menu, extension, client, settings) {
    const open = new PopupMenuItem(_('Open GPaste'));
    open.connect('activate', () => GPaste.util_spawn('Ui'));

    const empty = new PopupMenuItem(_('Empty History'));
    empty.connect('activate', () => {
        // No name, no history to empty: the property is cached off the daemon,
        // and reads back null while it is away.
        const history = client.get_history_name();

        if (history)
            GPaste.util_empty_with_confirmation(client, settings, history);
    });

    const preferences = new PopupMenuItem(_('Preferences'));
    preferences.connect('activate', () => extension.openPreferences());

    menu.addMenuItem(new PopupSeparatorMenuItem());
    menu.addMenuItem(open);
    menu.addMenuItem(empty);
    menu.addMenuItem(preferences);

    return {open, empty, preferences};
}
