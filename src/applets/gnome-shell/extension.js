/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2012 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

const Main = imports.ui.main;
const Panel = imports.ui.panel;
const Lang = imports.lang;
const Gio = imports.gi.Gio;
const Pango = imports.gi.Pango;
const PanelMenu = imports.ui.panelMenu;
const PopupMenu = imports.ui.popupMenu;
const Gettext = imports.gettext;
const GPaste = imports.gi.GPaste;

const _ = Gettext.domain('GPaste').gettext;
const BUS_NAME = 'org.gnome.GPaste';
const OBJECT_PATH = '/org/gnome/GPaste';

const GPasteIndicator = new Lang.Class({
    Name: 'GPasteIndicator',
    Extends: PanelMenu.SystemStatusButton,

    _init: function() {
        this.parent('edit-paste-symbolic');
        this._killSwitch = new PopupMenu.PopupSwitchMenuItem(_("Track changes"), true);
        this._killSwitch.connect('toggled', Lang.bind(this, this._toggleDaemon));
        this._client = new GPaste.Client();
        this._client.connect('changed', Lang.bind(this, this._updateHistory));
        this._client.connect('show-history', Lang.bind(this, this._showHistory));
        this._client.connect('tracking', Lang.bind(this, function(trackingState) {
            this._trackingStateChanged(trackingState);
        }));
        this._createHistory();
        this._noHistory = new PopupMenu.PopupMenuItem("");
        this._noHistory.setSensitive(false);
        this._emptyHistory = new PopupMenu.PopupMenuItem(_("Empty history"));
        this._emptyHistory.connect('activate', Lang.bind(this, this._empty));
        this._fillMenu();
    },

    _select: function(index) {
        this._client.select(index);
    },

    _delete: function(index) {
        this._client.delete(index);
    },

    _empty: function() {
        this._client.empty();
    },

    _trackingStateChanged: function(trackingState) {
        this._killSwitch.setToggleState(trackingState);
    },

    _toggleDaemon: function() {
        this._client.track(this._killSwitch.state);
    },

    _fillMenu: function() {
        this._killSwitch.setToggleState(this._client.is_active());
        this.menu.addMenuItem(this._killSwitch);
        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        this._addHistoryItems();
        this.menu.addMenuItem(this._noHistory);
        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        this.menu.addMenuItem(this._emptyHistory);
        this.menu.addSettingsAction(_("GPaste daemon settings"), 'gpaste-settings.desktop');
        this._updateHistory();
    },

    _updateHistory: function() {
        let history = this._client.get_history();
        if (history != null && history.length != 0) {
            let limit = Math.min(history.length, this._history.length);
            for (let index = 0; index < limit; ++index)
                this._updateHistoryItem(index, history[index]);
            this._hideHistory(limit);
            this._noHistory.actor.hide();
            this._emptyHistory.actor.show();
        } else {
            this._noHistory.label.text = (history == null) ? _("(Couldn't connect to GPaste daemon)") : _("(Empty)");
            this._hideHistory();
            this._emptyHistory.actor.hide();
            this._noHistory.actor.show();
        }
    },

    _showHistory: function() {
        this.menu.open(true);
    },

    _createHistoryItem: function(index) {
        let item = new PopupMenu.PopupAlternatingMenuItem("");
        item.actor.set_style_class_name('popup-menu-item');
        let label = item.label;
        label.clutter_text.max_length = 60;
        label.clutter_text.ellipsize = Pango.EllipsizeMode.END;
        item.connect('activate', Lang.bind(this, function(actor, event) {
            if (item.state == PopupMenu.PopupAlternatingMenuItemState.DEFAULT) {
                this._select(index);
                return false;
            } else {
                this._delete(index);
                return true;
            }
        }));
        return item;
    },

    _createHistory: function() {
        this._history = [];
        for (let index = 0; index < 20; ++index)
            this._history[index] = this._createHistoryItem(index);
        this._history[0].actor.set_style("font-weight: bold;");
    },

    _addHistoryItems: function() {
        for (let index = 0; index < this._history.length; ++index)
            this.menu.addMenuItem(this._history[index]);
    },

    _updateHistoryItem: function(index, element) {
        let displayStr = element.replace(/\n/g, ' ');
        let altDisplayStr = _("delete: %s").format(displayStr);
        this._history[index].updateText(displayStr, altDisplayStr);
        this._history[index].actor.show();
    },

    _hideHistory: function(startIndex) {
        for (let index = startIndex || 0; index < this._history.length; ++index)
            this._history[index].actor.hide();
    },

    _onStateChanged: function (state) {
        this._client.on_extension_state_changed(state);
    }
});

let _indicator;

function init(extension) {
    Gettext.bindtextdomain('gpaste', extension.metadata.localedir);
}

function enable() {
    _indicator = new GPasteIndicator();
    _indicator._onStateChanged (true);
    Main.panel.addToStatusArea('gpaste', _indicator);
}

function disable() {
    _indicator._onStateChanged (false);
    _indicator.destroy();
}

