/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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
const Lang = imports.lang;
const Gio = imports.gi.Gio;
const Pango = imports.gi.Pango;
const PanelMenu = imports.ui.panelMenu;
const PopupMenu = imports.ui.popupMenu;
const Gettext = imports.gettext;

const _ = Gettext.domain('GPaste').gettext;
const BUS_NAME = 'org.gnome.GPaste';
const OBJECT_PATH = '/org/gnome/GPaste';

const GPasteInterface =
    <interface name="org.gnome.GPaste">
        <method name="GetHistory">
            <arg type="as" direction="out" />
        </method>
        <method name="Select">
            <arg type="u" direction="in" />
        </method>
        <method name="Delete">
            <arg type="u" direction="in" />
        </method>
        <method name="Empty" />
        <method name="Track">
            <arg type="b" direction="in" />
        </method>
        <method name="OnExtensionStateChanged">
            <arg type="b" direction="in" />
        </method>
        <signal name="Changed" />
        <signal name="ToggleHistory" />
        <signal name="Tracking">
            <arg type="b" direction="out" />
        </signal>
        <property name="Active" type="b" access="read" />
    </interface>;
const GPasteProxy = Gio.DBusProxy.makeProxyWrapper(GPasteInterface);

function GPasteIndicator() {
    this._init.apply(this, arguments);
}

GPasteIndicator.prototype = {
    __proto__: PanelMenu.SystemStatusButton.prototype,

    _init: function() {
        PanelMenu.SystemStatusButton.prototype._init.call(this, 'edit-paste-symbolic');
        this._killSwitch = new PopupMenu.PopupSwitchMenuItem(_("Track changes"), true);
        this._killSwitch.connect('toggled', Lang.bind(this, this._toggleDaemon));
        this._proxy = new GPasteProxy(Gio.DBus.session, BUS_NAME, OBJECT_PATH);
        this._proxy.connectSignal('Changed', Lang.bind(this, this._updateHistory));
        this._proxy.connectSignal('ToggleHistory', Lang.bind(this, this._toggleHistory));
        this._proxy.connectSignal('Tracking', Lang.bind(this, function(proxy, sender, [trackingState]) {
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
        let active = this._proxy.Active;
        if (active != null)
            this._killSwitch.setToggleState(active);
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
        this._proxy.GetHistoryRemote(Lang.bind(this, function(result, err) {
            let [history] = err ? [null] : result;
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
        }));
    },

    _toggleHistory: function() {
        this.menu.toggle();
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
        this._proxy.OnExtensionStateChangedRemote(state);
    }
};

let _indicator;

function init(metadata) {
    Gettext.bindtextdomain('gpaste', metadata.localedir);
    StatusIconDispatcher.STANDARD_TRAY_ICON_IMPLEMENTATIONS['gpaste-applet'] = 'gpaste';
    Panel.STANDARD_STATUS_AREA_ORDER.unshift('gpaste');
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

