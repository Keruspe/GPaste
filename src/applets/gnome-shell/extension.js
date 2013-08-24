/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

const Clutter = imports.gi.Clutter;
const Gettext = imports.gettext;
const GPaste = imports.gi.GPaste;
const Lang = imports.lang;
const Main = imports.ui.main;
const PanelMenu = imports.ui.panelMenu;
const Pango = imports.gi.Pango;
const PopupMenu = imports.ui.popupMenu;
const St = imports.gi.St;

const _ = Gettext.domain('GPaste').gettext;
const BUS_NAME = 'org.gnome.GPaste';
const OBJECT_PATH = '/org/gnome/GPaste';

const GPasteStatusIcon = new Lang.Class({
    Name: 'GPasteStatusIcon',
    Extends: St.BoxLayout,

    _init: function() {
        this.parent({ style_class: 'panel-status-menu-box' });

        this.add_child(new St.Icon({
            icon_name: 'edit-paste-symbolic',
            style_class: 'system-status-icon'
        }));

        this.add_child(new St.Label({
            text: '\u25BE',
            y_expand: true,
            y_align: Clutter.ActorAlign.CENTER
        }));
    }
});

const GPasteDeleteButton = new Lang.Class({
    Name: 'GPasteDeleteButton',
    Extends: St.Button,

    _init: function(client, index) {
        this.parent();

        this.child = new St.Icon({
            icon_name: 'edit-delete-symbolic',
            style_class: 'system-status-icon'
        });

        this.connect('clicked', function() {
            client.delete(index);
            return true;
        });

    }
});

const GPasteDeleteMenuItemPart = new Lang.Class({
    Name: 'GPasteDeleteMenuItemPart',
    Extends: St.Bin,

    _init: function(client, index) {
        this.parent({ x_align: St.Align.END });
        
        this.child = new GPasteDeleteButton(client, index);
    }
});

const GPasteMenuItem = new Lang.Class({
    Name: 'GPasteMenuItem',
    Extends: PopupMenu.PopupMenuItem,

    _init: function(client, index) {
        this.parent("");

        let text = this.label.clutter_text;
        text.max_length = 60;
        text.ellipsize = Pango.EllipsizeMode.END;

        this.connect('activate', Lang.bind(this, function(actor, event) {
            client.select(index);
        }));

        this.actor.connect('key-press-event', Lang.bind(this, function(actor, event) {
            let symbol = event.get_key_symbol();
            if (symbol == Clutter.KEY_BackSpace || symbol == Clutter.KEY_Delete) {
                client.delete(index);
                return true;
            }
            return false;
        }));

        this.actor.add(new GPasteDeleteMenuItemPart(client, index), { expand: true, x_align: St.Align.END });
    }
});

const GPasteStateSwitch = new Lang.Class({
    Name: 'GPasteStateSwitch',
    Extends: PopupMenu.PopupSwitchMenuItem,

    _init: function(client) {
        this.parent(_("Track changes"), client.is_active());

        this._fromDaemon = false;

        client.connect('tracking', Lang.bind(this, function(c, state) {
            this._fromDaemon = true;
            this.setToggleState(state);
            this._fromDaemon = false;
        }));

        this.connect('toggled', Lang.bind(this, function() {
            if (!this._fromDaemon);
                client.track(this.state);
        }));
    }
});

const GPasteIndicator = new Lang.Class({
    Name: 'GPasteIndicator',
    Extends: PanelMenu.Button,

    _init: function() {
        this.parent(0.0, "GPaste");

        this.actor.add_child(new GPasteStatusIcon());

        this._client = new GPaste.Client();

        this._killSwitch = new GPasteStateSwitch(this._client);

        this._history = [];
        for (let index = 0; index < 20; ++index)
            this._history[index] = new GPasteMenuItem(this._client, index);
        this._history[0].label.set_style("font-weight: bold;");

        this._noHistory = new PopupMenu.PopupMenuItem("");
        this._noHistory.setSensitive(false);
        this._emptyHistory = new PopupMenu.PopupMenuItem(_("Empty history"));
        this._emptyHistory.connect('activate', Lang.bind(this, this._empty));
        this._fillMenu();

        this._client.connect('changed', Lang.bind(this, this._updateHistory));
        this._client.connect('show-history', Lang.bind(this, this._showHistory));
    },

    _empty: function() {
        this._client.empty();
    },

    _toggleDaemon: function() {
        this._client.track(this._killSwitch.state);
    },

    _fillMenu: function() {
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
        this.menu.firstMenuItem.setActive(true);
    },

    _addHistoryItems: function() {
        for (let index = 0; index < this._history.length; ++index)
            this.menu.addMenuItem(this._history[index]);
    },

    _updateHistoryItem: function(index, element) {
        let displayStr = element.replace(/\n/g, ' ');
        let altDisplayStr = _("delete: %s").format(displayStr);
        this._history[index].label.set_text (displayStr);
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

