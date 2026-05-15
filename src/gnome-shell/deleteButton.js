/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import GObject from 'gi://GObject?version=2.0';
import St from 'gi://St';

export const GPasteDeleteButton = GObject.registerClass(
class GPasteDeleteButton extends St.Button {
    _init(client, uuid) {
        super._init();

        this.child = new St.Icon({
            icon_name: 'edit-delete-symbolic',
            style_class: 'popup-menu-icon'
        });

        this._client = client;
        this.setUuid(uuid);
    }

    setUuid(uuid) {
        this._uuid = uuid;
    }

    vfunc_clicked(_clickedButton) {
        this._client.delete(this._uuid, null);
    }
});
