/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const { GLib, GObject, GPaste, St } = imports.gi;

var GPasteEmptyHistoryItem = GObject.registerClass(
class GPasteEmptyHistoryItem extends St.Button {
    _init(client, settings, menu) {
        super._init({
            x_expand: true,
            x_align: St.Align.MIDDLE,
            reactive: true,
            can_focus: true,
            track_hover: true,
            style_class: 'button',
            child: new St.Icon({
                icon_name: 'edit-clear-all-symbolic',
                style_class: 'popup-menu-icon'
            })
        });

        this.connect('clicked', function() {
            menu.itemActivated();
            client.get_history_name((client, result) => {
                const name = client.get_history_name_finish(result);

                GPaste.util_empty_with_confirmation (client, settings, name);
            });
        });
    }
});
