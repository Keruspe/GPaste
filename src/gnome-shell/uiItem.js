/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const { GObject, GPaste, St } = imports.gi;

var GPasteUiItem = GObject.registerClass(
class GPasteUiItem extends St.Button {
    _init(menu) {
        super._init({
            x_expand: true,
            x_align: St.Align.MIDDLE,
            reactive: true,
            can_focus: true,
            track_hover: true,
            style_class: 'button',
            child: new St.Icon({
                icon_name: 'go-home-symbolic',
                style_class: 'popup-menu-icon'
            })
        });

        this.connect('clicked', function() {
            menu.itemActivated();
            GPaste.util_spawn('Ui');
        });
    }
});
