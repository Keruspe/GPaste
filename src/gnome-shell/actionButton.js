// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import St from 'gi://St';

import {GPasteActionButtonActor} from './actionButtonActor.js';

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
            child: new GPasteActionButtonActor(iconName, label),
        });

        this._action = action;
    }

    vfunc_clicked(_clickedButton) {
        this._action();
    }
});
