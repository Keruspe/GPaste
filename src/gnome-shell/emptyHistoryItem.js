/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import { gettext as _ } from 'resource:///org/gnome/shell/extensions/extension.js';

import GObject from 'gi://GObject?version=2.0';
import GPaste from 'gi://GPaste?version=2';
import St from 'gi://St';

import { GPasteActionButton } from './actionButton.js';

export const GPasteEmptyHistoryItem = GObject.registerClass(
class GPasteEmptyHistoryItem extends GPasteActionButton {
    _init(client, settings, menu) {
        super._init('edit-clear-all-symbolic', _("Empty history"), async () => {
            menu.itemActivated();
            const name = await client.get_history_name(null);
            GPaste.util_empty_with_confirmation(client, settings, name);
        });
    }
});
