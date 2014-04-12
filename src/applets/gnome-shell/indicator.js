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

const PanelMenu = imports.ui.panelMenu;
const PopupMenu = imports.ui.popupMenu;

const GPaste = imports.gi.GPaste;

const Me = ExtensionUtils.getCurrentExtension();
const StatusIcon = Me.imports.statusIcon;

const GPasteIndicator = new Lang.Class({
    Name: 'GPasteIndicator',
    Extends: PanelMenu.Button,

    _init: function() {
        this.parent(0.0, "GPaste");

        this.actor.add_child(new StatusIcon.GPasteStatusIcon());

        GPaste.Client.new(Lang.bind(this, function (obj, result) {
            this._client = GPaste.Client.new_finish(result);

            this._header_size = 0;
            this._postHeaderSize = 0;
            this._history = [];
            this._preFooterSize = 0;
            this._footerSize = 0;

            //this._dummyHistoryItem = new GPasteDummyHistoryItem();

            //this._addToHeader(new GPasteStateSwitch(this._client));
            this._addToPostHeader(new PopupMenu.PopupSeparatorMenuItem());
            //this._addToPostHeader(this._dummyHistoryItem);
            this._addToPreFooter(new PopupMenu.PopupSeparatorMenuItem());
            //this._addToFooter(new GPasteEmptyHistoryItem(this._client));
            this._addSettingsAction();
            //this._addToFooter(new GPasteAboutMenuItem(this._client));

            this._onStateChanged (true);
        }));
    },

    shutdown: function() {
        this._onStateChanged (false);
        this.destroy();
    },

    _addToHeader: function(item) {
        this.menu.addMenuItem(item, this._headerSize++);
    },

    _addToPostHeader: function(item) {
        this.menu.addMenuItem(item, this._headerSize + this._postHeaderSize++);
    },

    _addToHistory: function(item) {
        this.menu.addMenuItem(item, this._headerSize + this._postHeaderSize + this._history.length);
        this._history[this._history.length] = item;
    },

    _addToPreFooter: function(item) {
        this.menu.addMenuItem(item, this._headerSize + this._postHeaderSize + this._history.length + this._preFooterSize++);
    },

    _addToFooter: function(item) {
        this.menu.addMenuItem(item, this._headerSize + this._postHeaderSize + this._history.length + this._preFooterSize + this._footerSize++);
    },

    _addSettingsAction: function() {
        // Simulate _addToFooter
        this.menu.addSettingsAction(_("GPaste daemon settings"), 'org.gnome.GPaste.Settings.desktop');
        ++this._footerSize;
    }

    _onStateChanged: function (state) {
        this._client.on_extension_state_changed(state, null);
    }
});

