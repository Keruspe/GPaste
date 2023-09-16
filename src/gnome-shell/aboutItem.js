/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2023, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import { gettext as _ } from 'resource:///org/gnome/shell/extensions/extension.js';

import GObject from 'gi://GObject';

import { GPasteActionButton } from './actionButton.js';

export const GPasteAboutItem = GObject.registerClass(
class GPasteAboutItem extends GPasteActionButton {
    _init(client, menu) {
        super._init('dialog-information-symbolic', _("About"), function() {
            menu.itemActivated();
            client.about(null);
        });
    }
});
