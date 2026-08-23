// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';
import {PopupSwitchMenuItem} from 'resource:///org/gnome/shell/ui/popupMenu.js';

import GObject from 'gi://GObject';

export const GPasteStateSwitch = GObject.registerClass(
class GPasteStateSwitch extends PopupSwitchMenuItem {
    constructor(client) {
        super(_('Track Clipboard Changes'), client.is_active());

        this._client = client;

        this.connect('toggled', this._onToggle.bind(this));
    }

    // The daemon telling us where it is, not the user asking it to move. Not
    // named toggle(): that is what PopupSwitchMenuItem.activate() calls, with no
    // argument, to flip the switch for the user -- overriding it would leave a
    // click setting the state to undefined instead. setToggleState() reaches
    // "toggled" all the same, through the switch's own notify::state, so the
    // echo back at the daemon that just told us is silenced here rather than by
    // the setter used.
    syncState(state) {
        this._fromDaemon = true;
        this.setToggleState(state);
        this._fromDaemon = false;
    }

    _onToggle(_item, state) {
        if (this._fromDaemon)
            return;

        this._client.set_active(state, null);
    }
});
