/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

imports.gi.versions.GPasteGtk = '4';

const ExtensionUtils = imports.misc.extensionUtils;

const { GPasteGtk } = imports.gi;

/** */
function init() {
    ExtensionUtils.initTranslations();
}

/**
 * @returns {Gtk.Widget} - the prefs widget
 */
function buildPrefsWidget() {
    return GPasteGtk.preferences_widget.new ();
}
