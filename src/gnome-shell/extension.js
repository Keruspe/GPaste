/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Config = imports.misc.config;

imports.gi.versions.Clutter = Config.LIBMUTTER_API_VERSION;
imports.gi.versions.GLib = '2.0';
imports.gi.versions.GPaste = '1.0';
imports.gi.versions.Pango = '1.0';
imports.gi.versions.St = '1.0';

const Gettext = imports.gettext;

const Main = imports.ui.main;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();

const Indicator = Me.imports.indicator;

function init(extension) {
    const metadata = extension.metadata;
    Gettext.bindtextdomain(metadata.gettext_package, metadata.localedir);
}

function disable() {
    Main.panel.statusArea.gpaste.shutdown();
}

function enable() {
    if (Main.panel.statusArea.gpaste)
        disable();
    Main.panel.addToStatusArea('gpaste', new Indicator.GPasteIndicator());
}
