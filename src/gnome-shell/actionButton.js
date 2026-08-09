// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import St from 'gi://St';

// The parent button fills its flex cell, so keep the icon+label group at its
// natural width and centered rather than stretched to the left edge.
function actionButtonActor(iconName, label) {
    const actor = new St.BoxLayout({
        style: 'spacing: 10px;',
        x_expand: false,
        x_align: Clutter.ActorAlign.CENTER,
    });

    actor.add_child(new St.Icon({
        icon_name: iconName,
        style_class: 'popup-menu-icon',
    }));
    actor.add_child(new St.Bin({child: new St.Label({text: label})}));

    return actor;
}

export const GPasteActionButton = GObject.registerClass(
class GPasteActionButton extends St.Button {
    constructor(iconName, label, action) {
        super({
            x_expand: true,
            // Fill the (equal) flex cell so all three action buttons end up the
            // same width; the icon+label inside stays centered (see the actor).
            x_align: Clutter.ActorAlign.FILL,
            reactive: true,
            can_focus: true,
            track_hover: true,
            style_class: 'button',
            child: actionButtonActor(iconName, label),
        });

        this._action = action;
    }

    vfunc_clicked(_clickedButton) {
        this._action();
    }
});
