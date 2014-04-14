/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Lang = imports.lang;

const PopupMenu = imports.ui.popupMenu;

const St = imports.gi.St;

const GPasteSearchItem = new Lang.Class({
    Name: 'GPasteSearchItem',
    Extends: PopupMenu.PopupBaseMenuItem,

    _init: function () {
        this.parent({reactive: false});

        this.label = new St.Label({ text: '' });
        this.actor.add(this.label);
        this.actor.label_actor = this.label;

        this._entry = new St.Entry({
            name: 'searchEntry',
            track_hover: true,
            reactive: true,
            can_focus: true
        });
        this.actor.add(new St.Bin({child: this._entry, x_align: St.Align.MIDDLE}), { expand: true });

        this._entry.set_primary_icon(new St.Icon({
            style_class:'search-entry-icon',
            icon_name:'edit-find-symbolic'
        }));
        this._entry.clutter_text.connect('text-changed', Lang.bind(this, this._onTextChanged));
    },

    getText: function() {
        return this._entry.get_text();
    },

    _onTextChanged: function(se, prop) {
        this.emit('text-changed');
    }
});
