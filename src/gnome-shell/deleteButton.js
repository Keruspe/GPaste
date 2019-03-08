/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Clutter = imports.gi.Clutter;
const St = imports.gi.St;

var GPasteDeleteButton = class {
    constructor(client, index) {
        this.actor = new St.Button();

        this.actor.child = new St.Icon({
            icon_name: 'edit-delete-symbolic',
            style_class: 'popup-menu-icon'
        });

        this._client = client;
        this.setIndex(index);

        this.actor.connect('clicked', this._onClick.bind(this));
    }

    setIndex: function(index) {
        this._index = index;
    }

    _onClick: function() {
        this._client.delete(this._index, null);
        return Clutter.EVENT_STOP;
    }
};
