// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

// Execute the shipped menu footer with doubles for the menu items, the shell's
// app system and GPaste: that Open GPaste launches the graphical tool through
// the shell, which hands it an activation token, and through GPaste only when
// the shell does not know the tool. Not Shell rendering.
const assert = require('node:assert/strict');
const vm = require('node:vm');
const {loadShipped} = require('./suite.cjs');

const launched = [];
let known = true;

class PopupMenuItem {
    constructor(label) { this.label = label; this._handlers = {}; }
    connect(signal, handler) { this._handlers[signal] = handler; }
    activate() { this._handlers.activate(); }
}

const context = vm.createContext({
    _: text => text,
    PopupMenuItem,
    PopupSeparatorMenuItem: class {},
    GPaste: {util_spawn: app => launched.push(['spawn', app])},
    Shell: {
        AppSystem: {
            get_default: () => ({
                lookup_app: id => (known ? {activate: () => launched.push(['activate', id])} : null),
            }),
        },
    },
});
loadShipped(context, {addGPasteFooter: 'addGPasteFooter'});

const menu = {addMenuItem() {}};
const {open} = context.addGPasteFooter(menu, {openPreferences() {}});

open.activate();
assert.deepEqual(launched, [['activate', 'org.gnome.GPaste.Ui.desktop']]);

known = false;
launched.length = 0;
open.activate();
assert.deepEqual(launched, [['spawn', 'Ui']]);

console.log('Actions: opening the graphical tool through the shell passed');
