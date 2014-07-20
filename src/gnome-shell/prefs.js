/*
 *      This file is part of GPaste.
 *
 *      Copyright 2013-2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Gettext = imports.gettext;

const Gtk = imports.gi.Gtk;

const GPaste = imports.gi.GPaste;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();

function init() {
    let metadata = Me.metadata;
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
