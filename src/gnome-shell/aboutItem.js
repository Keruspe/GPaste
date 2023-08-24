/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2023, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import { gettext as _ } from 'resource:///org/gnome/shell/extensions/extension.js';

import GObject from 'gi://GObject';
import St from 'gi://St';

import { GPasteActionButtonActor } from './actionButtonActor.js';

export const GPasteAboutItem = GObject.registerClass(
class GPasteAboutItem extends St.Button {
    _init(client, menu) {
        super._init({
            x_expand: true,
            x_align: St.Align.MIDDLE,
            reactive: true,
            can_focus: true,
            track_hover: true,
            style_class: 'button',
            child: new GPasteActionButtonActor('dialog-information-symbolic', _("About"))
        });

        this.connect('clicked', function() {
            menu.itemActivated();
            client.about(null);
        });
    }
});
