/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Gettext = imports.gettext;

const PopupMenu = imports.ui.popupMenu;

const _ = Gettext.domain('GPaste').gettext;

var GPasteStateSwitch = class extends PopupMenu.PopupSwitchMenuItem {
    constructor(client) {
        super(_("Track changes"), client.is_active());

        this._client = client;

        this.connect('toggled', this._onToggle.bind(this));
    }

    toggle(state) {
        if (state !== this.state)
            this.parent();
    }

    _onToggle(state) {
        this._client.track(this.state, null);
    }
};
