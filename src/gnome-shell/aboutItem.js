/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const { GObject, St } = imports.gi;

var GPasteAboutItem = GObject.registerClass(
class GPasteAboutItem extends St.Button {
    _init(client, menu) {
        super._init({
            x_expand: true,
            x_align: St.Align.MIDDLE,
            reactive: true,
            can_focus: true,
            track_hover: true,
            style_class: 'button',
            child: new St.Icon({
                icon_name: 'dialog-information-symbolic',
                style_class: 'popup-menu-icon'
            })
        });

        this.connect('clicked', function() {
            menu.itemActivated();
            client.about(null);
        });
    }
});
