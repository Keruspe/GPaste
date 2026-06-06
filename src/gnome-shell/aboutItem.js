// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';

import GObject from 'gi://GObject?version=2.0';

import {GPasteActionButton} from './actionButton.js';

export const GPasteAboutItem = GObject.registerClass(
class GPasteAboutItem extends GPasteActionButton {
    _init(client, menu) {
        super._init('dialog-information-symbolic', _('About'), () => {
            menu.itemActivated();
            client.about(null);
        });
    }
});
