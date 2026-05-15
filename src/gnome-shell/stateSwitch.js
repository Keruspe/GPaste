/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import { gettext as _ } from 'resource:///org/gnome/shell/extensions/extension.js';
import { PopupSwitchMenuItem } from 'resource:///org/gnome/shell/ui/popupMenu.js';

import GObject from 'gi://GObject?version=2.0';

export const GPasteStateSwitch = GObject.registerClass(
class GPasteStateSwitch extends PopupSwitchMenuItem {
    _init(client) {
        super._init(_("Track changes"), client.is_active());

        this._client = client;

        this.connect('toggled', this._onToggle.bind(this));
    }

    toggle(state) {
        if (state !== this.state)
            super.toggle(state);
    }

    _onToggle(state) {
        this._client.track(this.state, null);
    }
});
