/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2023, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import GObject from 'gi://GObject';
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
