// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';

import GObject from 'gi://GObject';
import GPaste from 'gi://GPaste?version=3';

import {GPasteActionButton} from './actionButton.js';

export const GPasteEmptyHistoryItem = GObject.registerClass(
class GPasteEmptyHistoryItem extends GPasteActionButton {
    constructor(client, settings, menu) {
        // The button invokes the action synchronously and drops what it returns,
        // so this promise is nobody's to await: catch here or a daemon that goes
        // away mid-call surfaces as an unhandled rejection.
        super('edit-clear-all-symbolic', _('Empty history'), () => {
            menu.itemActivated();
            client.get_history_name()
                .then(name => GPaste.util_empty_with_confirmation(client, settings, name))
                .catch(console.error);
        });
    }
});
