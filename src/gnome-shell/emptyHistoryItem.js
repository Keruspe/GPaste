/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2023, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import { gettext as _ } from 'resource:///org/gnome/shell/extensions/extension.js';

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import GPaste from 'gi://GPaste';
import St from 'gi://St';

import { GPasteActionButtonActor } from './actionButtonActor.js';

export const GPasteEmptyHistoryItem = GObject.registerClass(
class GPasteEmptyHistoryItem extends St.Button {
    _init(client, settings, menu) {
        super._init({
            x_expand: true,
            x_align: Clutter.ActorAlign.START,
            reactive: true,
            can_focus: true,
            track_hover: true,
            style_class: 'button',
            child: new GPasteActionButtonActor('edit-clear-all-symbolic', _("Empty history"))
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
