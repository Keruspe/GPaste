/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2017, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

imports.gi.versions.Clutter = '1';

const Lang = imports.lang;

const Clutter = imports.gi.Clutter;
const St = imports.gi.St;

var GPasteDeleteButton = new Lang.Class({
    Name: 'GPasteDeleteButton',

    _init: function(client, index) {
        this.actor = new St.Button();

        this.actor.child = new St.Icon({
            icon_name: 'edit-delete-symbolic',
            style_class: 'popup-menu-icon'
        });

        this._client = client;
        this.setIndex(index);

        this.actor.connect('clicked', Lang.bind(this, this._onClick));
    },

    setIndex: function(index) {
        this._index = index;
    },

    _onClick: function() {
        this._client.delete(this._index, null);
        return Clutter.EVENT_STOP;
    }
});
