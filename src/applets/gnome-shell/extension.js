/*
 *      This program is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *      Copyright 2011 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      This file is part of GPaste.
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

const StatusIconDispatcher = imports.ui.statusIconDispatcher;
const Main = imports.ui.main;
const Panel = imports.ui.panel;
const DBus = imports.dbus;
const Lang = imports.lang;
const St = imports.gi.St;
const GLib = imports.gi.GLib;
const Pango = imports.gi.Pango;
const PanelMenu = imports.ui.panelMenu;
const PopupMenu = imports.ui.popupMenu;
const Util = imports.misc.util;
const Gettext = imports.gettext;

const _ = Gettext.domain('GPaste').gettext;

const BUS_NAME = 'org.gnome.GPaste';
const OBJECT_PATH = '/org/gnome/GPaste';

let pkglibexecdir = null;

const GPasteInterface = {
    name: BUS_NAME,
    methods: [
        { name: 'GetHistory', inSignature: '', outSignature: 'as' },
        { name: 'Select', inSignature: 'u', outSignature: '' },
        { name: 'Delete', inSignature: 'u', outSignature: '' },
        { name: 'Empty', inSignature: '', outSignature: '' },
        { name: 'Track', inSignature: 'b', outSignature: '' },
    ],
    signals: [
        { name: 'Changed', inSignature: '' },
        { name: 'Tracking', inSignature: 'b' },
    ],
    properties: [
        { name: 'Active', signature: 'b', access: 'readonly' },
    ]
};
let GPasteProxy = DBus.makeProxyClass(GPasteInterface);

function GPasteIndicator() {
    this._init.apply(this, arguments);
}

GPasteIndicator.prototype = {
    __proto__: PanelMenu.SystemStatusButton.prototype,

    _init: function() {
        this._connectedSignals = [ ];
        PanelMenu.SystemStatusButton.prototype._init.call(this, 'edit-paste-symbolic');
        this._killSwitch = new PopupMenu.PopupSwitchMenuItem(_("Track clipboard changes"), true);
        let connectId = this._killSwitch.connect('toggled', Lang.bind(this, this._toggleDaemon));
        this._connectedSignals.push({ obj: this._killSwitch, id: connectId });
        this._proxy = new GPasteProxy(DBus.session, BUS_NAME, OBJECT_PATH);
        connectId = this._proxy.connect('Changed', Lang.bind(this, this._fillHistory));
        this._connectedSignals.push({ obj: this._proxy, id: connectId });
        connectId = this._proxy.connect('Tracking', Lang.bind(this, function(proxy, trackingState) {
            this._trackingStateChanged(trackingState);
        }));
        this._connectedSignals.push({ obj: this._proxy, id: connectId });
        this._history = new PopupMenu.PopupMenuSection();
        this._fillMenu();
    },

    _select: function(index) {
        this._proxy.SelectRemote(index);
    },

    _delete: function(index) {
        this._proxy.DeleteRemote(index);
    },

    _empty: function() {
        this._proxy.EmptyRemote();
    },

    _trackingStateChanged: function(trackingState) {
        this._killSwitch.setToggleState(trackingState);
    },

    _toggleDaemon: function() {
        this._proxy.TrackRemote(this._killSwitch.state);
    },

    _fillMenu: function() {
        this._proxy.GetRemote('Active', Lang.bind(this, function(active) {
            if (active != null)
                this._killSwitch.setToggleState(active);
            this.menu.addMenuItem(this._killSwitch);
            this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
            this.menu.addMenuItem(this._history);
            let prefsItem = new PopupMenu.PopupMenuItem(_("GPaste Settings"));
            let connectId = prefsItem.connect('activate', function() {
                Util.spawn([pkglibexecdir + '/gpaste-settings']);
            });
            this._connectedSignals.push({ obj: prefsItem, id: connectId });
            this.menu.addMenuItem(prefsItem);
            this._fillHistory();
        }));
    },

    _fillHistory: function(history) {
        this._proxy.GetHistoryRemote(Lang.bind(this, function(history) {
            this._history.removeAll();
            if (history != null && history.length != 0) {
                let limit = (history.length > 20) ? 20 : history.length;
                for (let index = 0; index < limit; ++index)
                    this._addSelection(index, history[index]);
                this._history.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
                let emptyItem = new PopupMenu.PopupMenuItem(_("Empty history"));
                let connectId = emptyItem.connect('activate', Lang.bind(this, this._empty));
                this._connectedSignals.push({ obj: emptyItem, id: connectId });
                this._history.addMenuItem(emptyItem);
            } else {
                let message = (history == null) ? _("(Couldn't connect to GPaste daemon)") : _("(Empty)");
                let emptyItem = new PopupMenu.PopupMenuItem(message, { reactive: false });
                this._history.addMenuItem(emptyItem);
                this._history.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
            }
        }));
    },

    _addSelection: function(index, element) {
        let displaystr = element.replace(/\n/g, ' ');
        let altdisplaystr = _("delete: %s").format(displaystr);
        let selection = new PopupMenu.PopupAlternatingMenuItem(displaystr, altdisplaystr);
        selection.actor.style_class = 'my-alternating-menu-item';
        selection.actor.add_style_class_name('popup-menu-item');
        let label = selection.label;
        label.clutter_text.max_length = 60;
        label.clutter_text.ellipsize = Pango.EllipsizeMode.END;
        let connectId = selection.connect('activate', Lang.bind(this, function(actor, event) {
            if (selection.state == PopupMenu.PopupAlternatingMenuItemState.DEFAULT) {
                this._select(index);
                return false;
            } else {
                this._delete(index);
                return true;
            }
        }));
        this._connectedSignals.push({ obj: selection, id: connectId });
        this._history.addMenuItem(selection);
    },

    enable: function() {
        Main.panel.addToStatusArea('gpaste', this);
    },

    disable: function() {
        for each (i in this._connectedSignals)
            i.obj.disconnect(i.id);
        this.destroy();
    }
};

function init(metadata) {
    Gettext.bindtextdomain('gpaste', metadata.localedir);
    pkglibexecdir = metadata.pkglibexecdir;
    StatusIconDispatcher.STANDARD_TRAY_ICON_IMPLEMENTATIONS['gpaste-applet'] = 'gpaste';
    Panel.STANDARD_TRAY_ICON_ORDER.unshift('gpaste');
    Util.spawn([pkglibexecdir + '/gpasted']);
    return new GPasteIndicator();
}

