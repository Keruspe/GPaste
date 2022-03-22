/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const ExtensionUtils = imports.misc.extensionUtils;
const _ = ExtensionUtils.gettext;

const { GObject, St } = imports.gi;

var GPasteActionButtonActor = GObject.registerClass(
class GPasteActionButtonActor extends St.BoxLayout {
    _init(iconName, label) {
        super._init({
            x_expand: true,
            x_align: St.Align.START,
        });

        this.add_child(new St.Icon({
            icon_name: iconName,
            style_class: 'popup-menu-icon'
        }));

        this.add_child(new St.Bin({
            child: new St.Label({
                text: label,
                y_align: St.Align.MIDDLE,
            }),
        }));
    }
});
