/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const Gettext = imports.gettext;

const { GPaste } = imports.gi;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();

function init() {
    const metadata = Me.metadata;
    Gettext.bindtextdomain(metadata.gettext_package, metadata.localedir);
    Gettext.textdomain(metadata.gettext_package);
}

function buildPrefsWidget() {
    let widget = new GPaste.SettingsUiWidget({ margin: 12 });
    if (widget) {
        widget.show_all();
    }
    return widget;
}
