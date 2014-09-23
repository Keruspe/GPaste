/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

const Lang = imports.lang;

const Shell = imports.gi.Shell;
const St = imports.gi.St;

const GPastePrefsItem = new Lang.Class({
    Name: 'GPastePrefsItem',
    Extends: St.Button,

    _init: function(menu) {
        this.parent({
            reactive: true,
            can_focus: true,
            track_hover: true,
            style_class: 'system-menu-action'
        });

        this.child = new St.Icon({ icon_name: 'preferences-system-symbolic' });

        this.connect('clicked', function() {
            menu.itemActivated();
            Shell.AppSystem.get_default().lookup_app('org.gnome.GPaste.Settings.desktop').activate();
        });
    }
});
