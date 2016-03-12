/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Gettext = imports.gettext;

const Main = imports.ui.main;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();

const Indicator = Me.imports.indicator;

function init(extension) {
    let metadata = extension.metadata;
    Gettext.bindtextdomain(metadata.gettext_package, metadata.localedir);
}

function enable() {
    Main.panel.addToStatusArea('gpaste', new Indicator.GPasteIndicator());
}

function disable() {
    Main.panel.statusArea.gpaste.shutdown();
}

