/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

const Applet = imports.ui.applet;

const Me = imports.ui.appletManager.applets["GPaste@gnome-shell-extensions.gnome.org"];

function GPasteApplet(orientation, panel_height, instance_id) {
  this._init(orientation, panel_height, instance_id);
}

GPasteApplet.prototype = {
    __proto__: Applet.IconApplet.prototype,

    _init: function(orientation, panel_height, instance_id) {
        Applet.IconApplet.prototype._init.call(this, orientation, panel_height, instance_id);

        this.set_applet_icon_name("edit-paste");
        this.set_applet_tooltip("GPaste")
    },

    on_applet_clicked: function() {
    }
};

function main(metadata, orientation, panel_height, instance_id) {
    Gettext.bindtextdomain(metadata.gettext_package, metadata.localedir);
    return new GPasteApplet(orientation, panel_height, instance_id);
}

