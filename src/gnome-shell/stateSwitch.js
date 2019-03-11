/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const Gettext = imports.gettext;

const PopupMenu = imports.ui.popupMenu;

const _ = Gettext.domain('GPaste').gettext;

class GPasteStateSwitch extends PopupMenu.PopupSwitchMenuItem {
    constructor(client) {
        super(_("Track changes"), client.is_active());

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
};
