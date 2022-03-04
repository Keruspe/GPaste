/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const Config = imports.misc.config;

imports.gi.versions.Clutter = Config.LIBMUTTER_API_VERSION;
imports.gi.versions.GLib = '2.0';
imports.gi.versions.GPaste = '2.0';
imports.gi.versions.Pango = '1.0';
imports.gi.versions.St = '1.0';

const Main = imports.ui.main;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();

const Indicator = Me.imports.indicator;

class Extension {
    constructor() {
        ExtensionUtils.initTranslations();
    }

    enable() {
        Main.panel.addToStatusArea('gpaste', new Indicator.GPasteIndicator());
    }

    disable() {
        Main.panel.statusArea.gpaste.shutdown();
    }
}


/** */
function init() {
    Me.imports.checkerBypass.bypass();
    return new Extension();
}
