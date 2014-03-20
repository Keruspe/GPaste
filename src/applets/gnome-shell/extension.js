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
            if (!this._fromDaemon)
                client.track(this.state, null);
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
            client.delete(index, null);
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

        this.actor.add(new GPasteDeleteMenuItemPart(client, index), { expand: true, x_align: St.Align.END });
    },

    setText: function(text) {
        this.label.set_text(text);
        this.actor.show();
    }
});

const GPasteDummyHistory = new Lang.Class({
    Name: 'GPasteDummyHistory',
    Extends: PopupMenu.PopupMenuItem,

    _init: function() {
        this.parent("");

        this.setSensitive(false);
    },

    update: function(error) {
        this.label.text = (error) ? _("(Couldn't connect to GPaste daemon)") : _("(Empty)");
    }
});

const GPasteHistoryWrapper = new Lang.Class({
    Name: 'GPasteHistoryWrapper',

    _init: function(client, menu, emptyHistoryItem) {
        this._history = [];

        for (let index = 0; index < 20; ++index) {
            let item = new GPasteMenuItem(client, index);
            this._history[index] = item;
            menu.addMenuItem(item);
        }

        this._history[0].label.set_style("font-weight: bold;");

        this._dummyHistory = new GPasteDummyHistory();

        menu.addMenuItem(this._dummyHistory);

        this._update(client, emptyHistoryItem);

        client.connect('changed', Lang.bind(this, function() {
            this._update(client, emptyHistoryItem);
        }));

        client.connect('show-history', function() {
            menu.open(true);
            menu._getMenuItems()[2].setActive(true);
        });

    },

    _update: function(client, emptyHistoryItem) {
        client.get_history(Lang.bind (this, function (client, result) {
            let history = client.get_history_finish (result);
            if (history != null && history.length != 0) {
                let limit = Math.min(history.length, this._history.length);
                for (let index = 0; index < limit; ++index)
                    this._history[index].setText(history[index].replace(/\n/g, ' '));
                this._hideHistory(limit);
                this._dummyHistory.actor.hide();
                emptyHistoryItem.actor.show();
            } else {
                this._dummyHistory.update(history == null);
                this._hideHistory();
                emptyHistoryItem.actor.hide();
                this._dummyHistory.actor.show();
            }
        }));
    },

    _hideHistory: function(startIndex) {
        for (let index = startIndex || 0; index < this._history.length; ++index)
            this._history[index].actor.hide();
    }
});

const GPasteEmptyHistoryMenuItem = new Lang.Class({
    Name: 'GPasteEmptyHistoryMenuItem',
    Extends: PopupMenu.PopupMenuItem,

    _init: function(client) {
        this.parent(_("Empty history"));

        this.connect('activate', function() {
            client.empty(null);
        });
    }
});

const GPasteIndicator = new Lang.Class({
    Name: 'GPasteIndicator',
    Extends: PanelMenu.Button,

    _init: function() {
        this.parent(0.0, "GPaste");

        this.actor.add_child(new GPasteStatusIcon());

        GPaste.Client.new(Lang.bind(this, function (obj, result) {
            this._client = GPaste.Client.new_finish (result);

            this.menu.addMenuItem(new GPasteStateSwitch(this._client));
            this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

            let emptyHistoryItem = new GPasteEmptyHistoryMenuItem(this._client);

            this._history = new GPasteHistoryWrapper(this._client, this.menu, emptyHistoryItem);

            this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
            this.menu.addMenuItem(emptyHistoryItem);
            this.menu.addSettingsAction(_("GPaste daemon settings"), 'org.gnome.GPaste.Settings.desktop');

            this._onStateChanged (true);
        }));
    },

    shutdown: function() {
        this._onStateChanged (false);
        this.destroy();
    },

    _onStateChanged: function (state) {
        this._client.on_extension_state_changed(state, null);
    }
});

function init(extension) {
    let metadata = extension.metadata;
    Gettext.bindtextdomain(metadata.gettext_package, metadata.localedir);
}

function enable() {
    Main.panel.addToStatusArea('gpaste', new GPasteIndicator());
}

function disable() {
    Main.panel.statusArea.gpaste.shutdown();
}

