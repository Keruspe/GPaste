// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';
import {PopupBaseMenuItem} from 'resource:///org/gnome/shell/ui/popupMenu.js';

import GObject from 'gi://GObject';
import St from 'gi://St';

import {addActionButton} from './actionButton.js';

// What can be done to an item besides selecting it, shown under its row on a
// right click -- the shape a history row's actions already have, and inline for
// the same reason: a popup menu opened from inside an open one would need a grab
// of its own.
//
// Pinning and deleting are not here, where a history row's Delete is: those two
// are on the item row itself, shown under the pointer or the keyboard, so there
// is nothing of them left for this to be where it is looked for. What is here is
// what the graphical tool keeps in its own item menu and had nowhere to go in
// the shell, in that menu's words.
//
// Only a text item has either action, so a row of another kind never opens this
// -- see GPasteItem. A row of buttons that are all insensitive would be an offer
// of nothing.
export const GPasteItemActionsItem = GObject.registerClass({
    Signals: {
        'strip-rich-text': {param_types: [GObject.TYPE_STRING]},
        'upload': {param_types: [GObject.TYPE_STRING]},
    },
}, class GPasteItemActionsItem extends PopupBaseMenuItem {
    constructor() {
        // Not activatable, as the history actions are not: what acts is a
        // button, and a row that activated would close the menu.
        super({
            activate: false,
            reactive: true,
            hover: false,
            can_focus: false,
        });

        this._uuid = null;

        this._buttons = new St.BoxLayout({
            x_expand: true,
            style: 'spacing: 6px;',
        });
        this._firstButton = addActionButton(this._buttons, _('Remove Rich Text'), () => this.emit('strip-rich-text', this._uuid));
        addActionButton(this._buttons, _('Upload'), () => this.emit('upload', this._uuid));
        this.add_child(this._buttons);

        this.reset();
    }

    // What these act on is the item they were opened for, named by its uuid
    // rather than read off the row at the click: a recycled row is between two
    // items while its fetch is out.
    reset(uuid = null) {
        this._uuid = uuid;
    }

    focus() {
        this._firstButton.grab_key_focus();
    }
});
