/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

imports.gi.versions.GLib = '2.0';
imports.gi.versions.GPaste = '1.0';
imports.gi.versions.St = '1.0';

const Lang = imports.lang;

const GLib = imports.gi.GLib;
const St = imports.gi.St;

const GPaste = imports.gi.GPaste;

var GPasteEmptyHistoryItem = new Lang.Class({
    Name: 'GPasteEmptyHistoryItem',

    _init: function(client) {
        this.actor = new St.Button({
            reactive: true,
            can_focus: true,
            track_hover: true,
            style_class: 'system-menu-action'
        });

        this.actor.child = new St.Icon({ icon_name: 'edit-clear-all-symbolic' });

        this.actor.connect('clicked', function() {
            client.get_history_name((client, result) => {
                const name = client.get_history_name_finish(result);

                GPaste.util_activate_ui("empty", GLib.Variant.new_string(name));
            });
        });
    }
});
