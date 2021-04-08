/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

imports.gi.versions.Gtk = '4.0';

const Config = imports.misc.config;

const Gettext = imports.gettext;

//const { GPaste } = imports.gi;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();

function init() {
    const domain = Me.metadata['gettext-domain'];
    Gettext.bindtextdomain(domain, Config.LOCALEDIR);
    Gettext.textdomain(domain);
}

function GPaste40PlaceHolder() {
    const { GLib, Gtk } = imports.gi;
    GLib.spawn_async(null, ["gpaste-client", "settings"], null, GLib.SpawnFlags.SEARCH_PATH | GLib.SpawnFlags.DO_NOT_REAP_CHILD, null);
    return new Gtk.Label({ label: "Opened the GPaste settings." });
}

function buildPrefsWidget() {
    /*
    let widget = new GPaste.SettingsUiWidget({ margin: 12 });
    if (widget) {
        widget.show_all();
    }
    */
    let widget = GPaste40PlaceHolder();
    return widget;
}
