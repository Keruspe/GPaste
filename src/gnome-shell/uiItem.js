// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';

import GObject from 'gi://GObject';
import GPaste from 'gi://GPaste?version=2';

import {GPasteActionButton} from './actionButton.js';

export const GPasteUiItem = GObject.registerClass(
class GPasteUiItem extends GPasteActionButton {
    constructor(menu) {
        super('go-home-symbolic', _('Graphical tool'), () => {
            menu.itemActivated();
            GPaste.util_spawn('Ui');
        });
    }
});
