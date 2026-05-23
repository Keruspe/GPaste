// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject?version=2.0';
import St from 'gi://St';

import { GPasteActionButtonActor } from './actionButtonActor.js';

export const GPasteActionButton = GObject.registerClass(
class GPasteActionButton extends St.Button {
    _init(iconName, label, action) {
        super._init({
            x_expand: true,
            x_align: Clutter.ActorAlign.CENTER,
            reactive: true,
            can_focus: true,
            track_hover: true,
            style_class: 'button',
            child: new GPasteActionButtonActor(iconName, label)
        });

        this._action = action;
    }

    vfunc_clicked(_clickedButton) {
        this._action();
    }
});
