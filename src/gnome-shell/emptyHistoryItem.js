/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const { GLib, GPaste, St } = imports.gi;

var GPasteEmptyHistoryItem = class {
    constructor(client, settings, menu) {
        this.actor = new St.Button({
            reactive: true,
            can_focus: true,
            track_hover: true,
            style_class: 'system-menu-action'
        });

        this.actor.child = new St.Icon({ icon_name: 'edit-clear-all-symbolic' });

        this.actor.connect('clicked', function() {
            menu.itemActivated();
            client.get_history_name((client, result) => {
                const name = client.get_history_name_finish(result);

                GPaste.util_empty_with_confirmation (client, settings, name);
            });
        });
    }
};
