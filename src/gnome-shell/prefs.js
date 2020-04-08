/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const Config = imports.misc.config;

const Gettext = imports.gettext;

const { GPaste } = imports.gi;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();

function init() {
    const domain = Me.metadata['gettext-domain'];
    Gettext.bindtextdomain(domain, Config.LOCALEDIR);
    Gettext.textdomain(domain);
}

function buildPrefsWidget() {
    let widget = new GPaste.SettingsUiWidget({ margin: 12 });
    if (widget) {
        widget.show_all();
    }
    return widget;
}
