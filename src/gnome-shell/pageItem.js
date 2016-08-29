/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Lang = imports.lang;

const St = imports.gi.St;

const GPastePageItem = new Lang.Class({
    Name: 'GPastePageItem',
    Extends: St.Button,

    _init: function(page) {
        this.parent({
            reactive: true,
            can_focus: true,
            track_hover: true,
            style_class: 'pager-button'
        });

        this.setPage(page);

        this.connect('clicked', Lang.bind(this, function() {
            this.emit('switch', this._page);
        }));
    },

    setPage: function(page) {
        if (page > 0) {
            this._page = page
            this.child = new St.Label({ text: '' + page });
            this.show();
        } else {
            this.hide();
        }
    }
});
