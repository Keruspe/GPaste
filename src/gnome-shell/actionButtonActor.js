/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2023, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import { gettext as _ } from 'resource:///org/gnome/shell/extensions/extension.js';

import GObject from 'gi://GObject';
import St from 'gi://St';

export const GPasteActionButtonActor = GObject.registerClass(
class GPasteActionButtonActor extends St.BoxLayout {
    _init(iconName, label) {
        super._init({ style: 'spacing: 10px;' });

        this.add_child(new St.Icon({
            icon_name: iconName,
            style_class: 'popup-menu-icon'
        }));

        this.add_child(new St.Bin({
            child: new St.Label({ text: label }),
        }));
    }
});
