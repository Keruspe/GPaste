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
 * actually reaches for there -- had none of. Nor is emptying the history: that
 * is done from a history's right click in the switcher, which can empty any of
 * them rather than only the one in use.
 *
 * @param {PopupMenu} menu - the indicator's menu
 * @param {Extension} extension - the extension, which owns the preferences
 * @returns {object} the two items, by name
 */
export function addGPasteFooter(menu, extension) {
    const open = new PopupMenuItem(_('Open GPaste'));
    open.connect('activate', () => GPaste.util_spawn('Ui'));

    const preferences = new PopupMenuItem(_('Preferences'));
    preferences.connect('activate', () => extension.openPreferences());

    menu.addMenuItem(new PopupSeparatorMenuItem());
    menu.addMenuItem(open);
    menu.addMenuItem(preferences);

    return {open, preferences};
}
