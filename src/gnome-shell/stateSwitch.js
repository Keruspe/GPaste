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
        this.connect('destroy', () => {
            this._destroyed = true;
        });
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

        // A refused call leaves the daemon's Active where it was, so no
        // 'tracking' comes to move the switch back: its reply has to.
        this._client.set_active(state, null, (client, result) => {
            try {
                client.set_active_finish(result);
            } catch (e) {
                console.error(e);
                if (!this._destroyed)
                    this.syncState(client.is_active());
            }
        });
    }
});
