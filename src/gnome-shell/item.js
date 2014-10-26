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

const GPaste = imports.gi.GPaste;

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

        if (index <= 10) {
            this._indexLabel = new St.Label({
                text: index + ': '
            });
            this._indexLabelVisible = false;
        }

        this.connect('activate', Lang.bind (this, this._onActivate));
        this.actor.connect('key-press-event', Lang.bind(this, this._onKeyPressed));

        this._deleteItem = new DeleteItemPart.GPasteDeleteItemPart(client, index);
        this.actor.add(this._deleteItem, { expand: true, x_align: St.Align.END });

        this._settingsChangedId = settings.connect('changed::element-size', Lang.bind(this, this._resetTextSize));
        this.label.clutter_text.ellipsize = Pango.EllipsizeMode.END;
        this._resetTextSize();

        this.setIndex(index);

        this._destroyed = false;
        this.actor.connect('destroy', Lang.bind(this, this._onDestroy));
    },

    showIndex: function(state) {
        if (state) {
            if (!this._indexLabelVisible) {
                this.actor.insert_child_at_index(this._indexLabel, 1);
            }
        } else if (this._indexLabelVisible) {
            this.actor.remove_child(this._indexLabel);
        }
        this._indexLabelVisible = state;
    },

    refresh: function() {
        this.setIndex(this._index);
    },

    setIndex: function(index) {
        let oldIndex = this._index || -1;
        this._index = index;

        if (index == 0) {
            this.label.set_style("font-weight: bold;");
        } else if (oldIndex == 0) {
            this.label.set_style(null);
        }

        this._deleteItem.setIndex(index);

        if (index != -1) {
            this._client.get_element(index, Lang.bind(this, function(client, result) {
                let text = client.get_element_finish(result).replace(/[\t\n\r]/g, '');
                if (this._destroyed || text == this.label.get_text()) {
                    return;
                }
                this.label.clutter_text.set_text(text);
                if (oldIndex == -1) {
                    this.actor.show();
                }
            }));
        } else {
            this.label.clutter_text.set_text(null);
            this.actor.hide();
        }
    },

    _resetTextSize: function() {
        this.label.clutter_text.max_length = this._settings.get_element_size();
    },

    _onActivate: function(actor, event) {
        this._client.select(this._index, null);
    },

    _onKeyPressed: function(actor, event) {
        let symbol = event.get_key_symbol();
        if (symbol == Clutter.KEY_BackSpace || symbol == Clutter.KEY_Delete) {
            this._client.delete(this._index, null);
            return Clutter.EVENT_STOP;
        }
        return Clutter.EVENT_PROPAGATE;
    },

    _onDestroy: function() {
        if (this._destroyed) {
            return;
        }
        this._destroyed = true;
        this._settings.disconnect(this._settingsChangedId);
    }
});
