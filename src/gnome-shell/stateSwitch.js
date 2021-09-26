/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const ExtensionUtils = imports.misc.extensionUtils;
const _ = ExtensionUtils.gettext;

const { GObject } = imports.gi;

const PopupMenu = imports.ui.popupMenu;

var GPasteStateSwitch = GObject.registerClass(
class GPasteStateSwitch extends PopupMenu.PopupSwitchMenuItem {
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
