/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const { St } = imports.gi;

var GPasteAboutItem = class {
    constructor(client, menu) {
        this.actor = new St.Button({
            reactive: true,
            can_focus: true,
            track_hover: true,
            style_class: 'system-menu-action'
        });

        this.actor.child = new St.Icon({ icon_name: 'dialog-information-symbolic' });

        this.actor.connect('clicked', function() {
            menu.itemActivated();
            client.about(null);
        });
    }
};
