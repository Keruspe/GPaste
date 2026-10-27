// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {PopupMenuItem, PopupSeparatorMenuItem} from 'resource:///org/gnome/shell/ui/popupMenu.js';

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';

import GPaste from 'gi://GPaste?version=3';
import Shell from 'gi://Shell';

/**
 * Open the graphical tool as the dash would: the shell's own launch hands it a
 * Wayland activation token, without which the shell's focus-stealing
 * prevention says a window it already has open is ready rather than raising
 * it, and a running one is raised by the shell itself. GPaste.util_spawn () is
 * for a tool installed without its desktop file, which has nothing else to
 * launch it by.
 */
function openUi() {
    const app = Shell.AppSystem.get_default().lookup_app('org.gnome.GPaste.Ui.desktop');

    if (app)
        app.activate();
    else
        GPaste.util_spawn('Ui');
}

/**
 * Add what the menu can do beyond listing the history, under the history it
 * lists.
 *
 * Plain menu items rather than a row of buttons: an item is focusable and the
 * menu walks between them on its own, where a non-reactive row holding buttons
 * lets no key event bubble through it, and every arrow would have to be
 * answered by hand from the menu actor.
 *
 * About is not among them. A panel menu is not where an application's about
 * dialog belongs, and the slot goes to Preferences, which is what a user
 * actually reaches for there. Nor is emptying the history: that is done from a
 * history's right click in the switcher, which can empty any of them rather
 * than only the one in use.
 *
 * @param {PopupMenu} menu - the indicator's menu
 * @param {Extension} extension - the extension, which owns the preferences
 * @returns {object} the two items, by name
 */
export function addGPasteFooter(menu, extension) {
    const open = new PopupMenuItem(_('Open GPaste'));
    open.connect('activate', () => openUi());

    const preferences = new PopupMenuItem(_('Preferences'));
    preferences.connect('activate', () => extension.openPreferences());

    menu.addMenuItem(new PopupSeparatorMenuItem());
    menu.addMenuItem(open);
    menu.addMenuItem(preferences);

    return {open, preferences};
}
