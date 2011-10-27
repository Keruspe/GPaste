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
        this._proxy.connectSignal('Changed', Lang.bind(this, this._fillHistory));
        this._proxy.connectSignal('ToggleHistory', Lang.bind(this, this._toggleHistory));
        this._proxy.connectSignal('Tracking', Lang.bind(this, function(proxy, sender, [trackingState]) {
            this._trackingStateChanged(trackingState);
        }));
        this._history = new PopupMenu.PopupMenuSection();
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
        this.menu.addMenuItem(this._history);
        this.menu.addMenuItem(this._emptyHistory);
        this.menu.addSettingsAction(_("GPaste daemon settings"), 'gpaste-settings.desktop');
        this._fillHistory();
    },

    _fillHistory: function() {
        this._proxy.GetHistoryRemote(Lang.bind(this, function(result, err) {
            let [history] = err ? [null] : result;
            this._history.removeAll();
            this._history.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
            if (history != null && history.length != 0) {
                let limit = (history.length > 20) ? 20 : history.length;
                for (let index = 0; index < limit; ++index)
                    this._addSelection(index, history[index]);
                this._emptyHistory.actor.show();
            } else {
                let message = (history == null) ? _("(Couldn't connect to GPaste daemon)") : _("(Empty)");
                let noHistory = new PopupMenu.PopupMenuItem(message, { reactive: false });
                this._history.addMenuItem(noHistory);
                this._emptyHistory.actor.hide();
            }
            this._history.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        }));
    },

    _toggleHistory: function() {
        this.menu.toggle();
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
        selection.connect('activate', Lang.bind(this, function(actor, event) {
            if (selection.state == PopupMenu.PopupAlternatingMenuItemState.DEFAULT) {
                this._select(index);
                return false;
            } else {
                this._delete(index);
                return true;
            }
        }));
        this._history.addMenuItem(selection);
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
    Main.panel.addToStatusArea('gpaste', _indicator);
}

function disable() {
    _indicator.destroy();
}

