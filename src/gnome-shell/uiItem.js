/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2023, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import { gettext as _ } from 'resource:///org/gnome/shell/extensions/extension.js';

import GObject from 'gi://GObject';
import GPaste from 'gi://GPaste';

import { GPasteActionButton } from './actionButton.js';

export const GPasteUiItem = GObject.registerClass(
class GPasteUiItem extends GPasteActionButton {
    _init(menu) {
        super._init('go-home-symbolic', _("Graphical tool"), function() {
            menu.itemActivated();
            GPaste.util_spawn('Ui');
        });
    }
});
