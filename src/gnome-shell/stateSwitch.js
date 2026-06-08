// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';
import {PopupSwitchMenuItem} from 'resource:///org/gnome/shell/ui/popupMenu.js';

import GObject from 'gi://GObject';

export const GPasteStateSwitch = GObject.registerClass(
class GPasteStateSwitch extends PopupSwitchMenuItem {
    constructor(client) {
        super(_('Track changes'), client.is_active());

        this._client = client;

        this.connect('toggled', this._onToggle.bind(this));
    }

    toggle(state) {
        if (state !== this.state)
            super.toggle(state);
    }

    _onToggle(_item, state) {
        this._client.track(state, null);
    }
});
