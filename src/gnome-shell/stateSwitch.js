/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2017, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Gettext = imports.gettext;
const Lang = imports.lang;

const PopupMenu = imports.ui.popupMenu;

const _ = Gettext.domain('GPaste').gettext;

const GPasteStateSwitch = new Lang.Class({
    Name: 'GPasteStateSwitch',
    Extends: PopupMenu.PopupSwitchMenuItem,

    _init: function(client) {
        this.parent(_("Track changes"), client.is_active());

        this._client = client;

        this.connect('toggled', Lang.bind(this, this._onToggle));
    },

    toggle: function(state) {
        if (state !== this.state)
            this.parent();
    },

    _onToggle: function(state) {
        this._client.track(this.state, null);
    }
});
