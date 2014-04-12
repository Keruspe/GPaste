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

const Clutter = imports.gi.Clutter;
const Pango = imports.gi.Pango;
const St = imports.gi.St;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();

const DeleteItemPart = Me.imports.deleteItemPart;

const GPasteItem = new Lang.Class({
    Name: 'GPasteItem',
    Extends: PopupMenu.PopupMenuItem,

    _init: function(client, settings, index) {
        this.parent("");

        this._client = client;
        this._settings = settings;
        this._index = index;

        this.connect('activate', function(actor, event) {
            client.select(index, null);
        });

        this.actor.connect('key-press-event', function(actor, event) {
            let symbol = event.get_key_symbol();
            if (symbol == Clutter.KEY_BackSpace || symbol == Clutter.KEY_Delete) {
                client.delete(index, null);
                return true;
            }
            return false;
        });

        this.actor.add(new DeleteItemPart.GPasteDeleteItemPart(client, index), { expand: true, x_align: St.Align.END });

        client.connect('changed', Lang.bind(this, function() {
            this._resetText();
        }));
        this._resetText();

        settings.connect('changed::element-size', Lang.bind(this, function() {
            this._resetTextSize();
        }));
        this.label.clutter_text.ellipsize = Pango.EllipsizeMode.END;
        this._resetTextSize();
    },

    _resetText: function() {
        this._client.get_element(this._index, function(text) {
            this.label.set_text(text);
        });
    },

    _resetTextSize: function() {
        this.label.clutter_text.max_length = this._settings.get_element_size();
    }
});
