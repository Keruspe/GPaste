/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const StatusIconDispatcher = imports.ui.statusIconDispatcher;
const Panel = imports.ui.panel;
const DBus = imports.dbus;
const Lang = imports.lang;
const St = imports.gi.St;
const Pango = imports.gi.Pango;
const PanelMenu = imports.ui.panelMenu;
const PopupMenu = imports.ui.popupMenu;
const Gettext = imports.gettext.domain('gnome-shell');

const _ = Gettext.gettext;

const BUS_NAME = 'org.gnome.GPaste';
const OBJECT_PATH = '/org/gnome/GPaste';

const GPasteInterface = {
    name: BUS_NAME,
    methods: [
        { name: 'GetHistory', inSignature: '', outSignature: 'as' },
        { name: 'Select', inSignature: 'u', outSignature: '' },
        { name: 'Delete', inSignature: 'u', outSignature: '' },
        { name: 'Empty', inSignature: '', outSignature: '' },
        //{ name: 'Start', inSignature: '', outSignature: '' },
        //{ name: 'Quit', inSignature: '', outSignature: '' },
        ],
    signals: [
        { name: 'Changed', inSignature: '', outSignature: '' },
        ],
    properties: [
        ]
};
let GPasteProxy = DBus.makeProxyClass(GPasteInterface);

function Indicator() {
    this._init.apply(this, arguments);
}

Indicator.prototype = {
    __proto__: PanelMenu.SystemStatusButton.prototype,

    _init: function() {
        PanelMenu.SystemStatusButton.prototype._init.call(this, 'edit-paste-symbolic');
        this._proxy = new GPasteProxy(DBus.session, BUS_NAME, OBJECT_PATH);
        this._fillMenu();
        this._proxy.connect('Changed', Lang.bind(this, this._fillMenu));
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
/*
    _toggleDaemon: function() {
        print("Toggled " + this._killSwitch.state);
        if (this._killSwitch.state)
            this._proxy.StartRemote();
        else
            this._proxy.QuitRemote();
    },
*/
    _fillMenu: function() {
        this.menu.removeAll();
        //this._killSwitch = new PopupMenu.PopupSwitchMenuItem(_("GPaste daemon"), true);
        //this._killSwitch.connect('toggled', Lang.bind(this, this._toggleDaemon));
        //this.menu.addMenuItem(this._killSwitch);
        //this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        this._proxy.GetHistoryRemote(Lang.bind(this, function(history) {
            if (history.length == 0) {
                let emptyItem = new PopupMenu.PopupMenuItem('(Empty)', {reactive: false});
                this.menu.addMenuItem(emptyItem);
            } else {
                let index;
                let limit = 20;
                if (history.length < 20) limit = history.length;
                for (index = 0; index < limit; ++index) {
                    let selection = new PopupMenu.PopupMenuItem(history[index].replace(/\n/g, ' '));
                    let label = selection.label;
                    let inner_index = index;
                    label.clutter_text.max_length = 60;
                    label.clutter_text.ellipsize = Pango.EllipsizeMode.END;
                    //selection.actor.button_mask = St.ButtonMask.ONE | St.ButtonMask.THREE;
                    selection.connect('activate', Lang.bind(this, function(actor, event) {
                        this._select(inner_index);
                    }));
                    let deletor = new St.Button({label: 'x'});
                    deletor.connect('clicked', Lang.bind(this, function(actor, event) {
                        this._delete(inner_index);
                    }));
                    selection.indicator = deletor;
                    selection.addActor(deletor);
                    this.menu.addMenuItem(selection);
                }
                this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
                let emptyItem = new PopupMenu.PopupMenuItem('Empty history');
                emptyItem.connect('activate', Lang.bind(this, this._empty));
                this.menu.addMenuItem(emptyItem)
            }
        }));
    }
};

function main() {
    StatusIconDispatcher.STANDARD_TRAY_ICON_IMPLEMENTATIONS['gpaste-applet'] = 'gpaste';
    Panel.STANDARD_TRAY_ICON_ORDER.unshift('gpaste');
    Panel.STANDARD_TRAY_ICON_SHELL_IMPLEMENTATION['gpaste'] = Indicator;
}
