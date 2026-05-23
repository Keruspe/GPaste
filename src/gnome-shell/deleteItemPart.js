// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import GObject from 'gi://GObject?version=2.0';
import St from 'gi://St';

import { GPasteDeleteButton } from './deleteButton.js';

export const GPasteDeleteItemPart = GObject.registerClass(
class GPasteDeleteItemPart extends St.Bin {
    _init(client, uuid) {
        super._init();
        this._deleteButton = new GPasteDeleteButton(client, uuid);
        this.child = this._deleteButton;
    }

    setUuid(uuid) {
        this._deleteButton.setUuid(uuid);
    }
});
