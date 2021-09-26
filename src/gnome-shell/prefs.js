/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

imports.gi.versions.Gtk = '4.0';

const ExtensionUtils = imports.misc.extensionUtils;

//const { GPaste } = imports.gi;

/** */
function init() {
    ExtensionUtils.initTranslations();
}

/**
 * @returns {Gtk.Label} - the placeholder label
 */
function GPaste40PlaceHolder() {
    const { GLib, Gtk } = imports.gi;
    GLib.spawn_async(null, ["gpaste-client", "settings"], null, GLib.SpawnFlags.SEARCH_PATH | GLib.SpawnFlags.DO_NOT_REAP_CHILD, null);
    return new Gtk.Label({ label: "Opened the GPaste settings." });
}

/**
 * @returns {Gtk.Widget} - the prefs widget
 */
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
