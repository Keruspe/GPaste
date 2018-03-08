/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Lang = imports.lang;

const PopupMenu = imports.ui.popupMenu;

const St = imports.gi.St;

var GPasteSearchItem = new Lang.Class({
    Name: 'GPasteSearchItem',
    Extends: PopupMenu.PopupBaseMenuItem,

    _init: function () {
        this.parent({
            activate: false,
            reactive: true,
            hover: false,
            can_focus: false
        });

        this._entry = new St.Entry({
            name: 'GPasteSearchEntry',
            style_class:'search-entry',
            track_hover: true,
            reactive: true,
            can_focus: true
        });
        this.actor.add(this._entry, { expand: true });

        this._entry.set_primary_icon(new St.Icon({
            style_class:'search-entry-icon',
            icon_name:'edit-find-symbolic'
        }));
        this._entry.clutter_text.connect('text-changed', this._onTextChanged.bind(this));

        this._clearIcon = new St.Icon({
            style_class: 'search-entry-icon',
            icon_name: 'edit-clear-symbolic'
        });
        this._iconClickedId = 0;
    },

    get text() {
        return this._entry.get_text();
    },

    resetSize: function(size) {
        this._entry.style = 'width: ' + size + 'em';
    },

    reset: function() {
        this._entry.text = '';
        let text = this._entry.clutter_text;
        text.set_cursor_visible(true);
        text.set_selection(0, 0);
    },

    grabFocus: function() {
        this._entry.grab_key_focus();
    },

    _onTextChanged: function(se, prop) {
        const dummy = (this.text.length == 0);
        this._entry.set_secondary_icon((dummy) ? null : this._clearIcon);
        if (!dummy && this._iconClickedId == 0) {
            this._iconClickedId = this._entry.connect('secondary-icon-clicked', this.reset.bind(this));
        }
        this.emit('text-changed');
    }
});
