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

    _init: function(page) {
        this.actor = new St.Button({
            reactive: true,
            can_focus: true,
            track_hover: true,
            style_class: 'pager-button'
        });

        this.setPage(page);
    },

    setPage: function(page) {
        if (page > 0) {
            this._page = page
            this.actor.child = new St.Label({ text: '' + page });
            this.actor.show();
        } else {
            this.actor.hide();
        }
    }
});
