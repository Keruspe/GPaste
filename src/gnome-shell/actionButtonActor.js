// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import St from 'gi://St';

export const GPasteActionButtonActor = GObject.registerClass(
class GPasteActionButtonActor extends St.BoxLayout {
    constructor(iconName, label) {
        // Stay centered inside the (wider) button rather than stretching, so
        // the icon and label keep together regardless of the button width.
        super({style: 'spacing: 10px;', x_align: Clutter.ActorAlign.CENTER});

        this.add_child(new St.Icon({
            icon_name: iconName,
            style_class: 'popup-menu-icon',
        }));

        this.add_child(new St.Bin({
            child: new St.Label({text: label}),
        }));
    }
});
