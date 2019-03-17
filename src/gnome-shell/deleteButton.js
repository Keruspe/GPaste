/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const { Clutter, St } = imports.gi;

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

    setIndex(index) {
        this._index = index;
    }

    _onClick() {
        this._client.delete(this._index, null);
        return Clutter.EVENT_STOP;
    }
};
