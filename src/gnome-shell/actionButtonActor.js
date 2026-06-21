// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import St from 'gi://St';

export const GPasteActionButtonActor = GObject.registerClass(
class GPasteActionButtonActor extends St.BoxLayout {
    constructor(iconName, label) {
        // The parent button fills its flex cell, so keep the icon+label group at
        // its natural width and centered rather than stretched to the left edge.
        super({
            style: 'spacing: 10px;',
            x_expand: false,
            x_align: Clutter.ActorAlign.CENTER,
        });

        this.add_child(new St.Icon({
            icon_name: iconName,
            style_class: 'popup-menu-icon',
        }));

        this.add_child(new St.Bin({
            child: new St.Label({text: label}),
        }));
    }
});
