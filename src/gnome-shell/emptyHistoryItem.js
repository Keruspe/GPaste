/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2023, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import { gettext as _ } from 'resource:///org/gnome/shell/extensions/extension.js';

import GObject from 'gi://GObject';
import GPaste from 'gi://GPaste';
import St from 'gi://St';

import { GPasteActionButton } from './actionButton.js';

export const GPasteEmptyHistoryItem = GObject.registerClass(
class GPasteEmptyHistoryItem extends GPasteActionButton {
    _init(client, settings, menu) {
        super._init('edit-clear-all-symbolic', _("Empty history"), function() {
            menu.itemActivated();
            client.get_history_name((client, result) => {
                const name = client.get_history_name_finish(result);

                GPaste.util_empty_with_confirmation (client, settings, name);
            });
        });
    }
});
