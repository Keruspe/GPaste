/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2017, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Gettext = imports.gettext;

const Gtk = imports.gi.Gtk;

const GPaste = imports.gi.GPaste;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();

function init() {
    const metadata = Me.metadata;
    Gettext.bindtextdomain(metadata.gettext_package, metadata.localedir);
    Gettext.textdomain(metadata.gettext_package);
}

function buildPrefsWidget() {
    let widget = new GPaste.SettingsUiWidget({ orientation: Gtk.Orientation.VERTICAL, margin: 12 });
    if (widget) {
        widget.show_all();
    }
    return widget;
}
