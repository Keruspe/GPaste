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

const Clutter = imports.gi.Clutter;
const St = imports.gi.St;

const GPasteStatusIcon = new Lang.Class({
    Name: 'GPasteStatusIcon',
    Extends: St.BoxLayout,

    _init: function() {
        this.parent({ style_class: 'panel-status-menu-box' });

        this.add_child(new St.Icon({
            icon_name: 'edit-paste-symbolic',
            style_class: 'system-status-icon'
        }));

        this.add_child(new St.Icon({
            style_class: 'popup-menu-arrow',
            icon_name: 'pan-down-symbolic',
            y_expand: true,
            y_align: Clutter.ActorAlign.CENTER
        }));
    }
});
