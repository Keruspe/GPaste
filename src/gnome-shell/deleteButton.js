// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';

import GObject from 'gi://GObject';
import St from 'gi://St';

export const GPasteDeleteButton = GObject.registerClass(
class GPasteDeleteButton extends St.Button {
    constructor(client, uuid) {
        super();

        this.child = new St.Icon({
            icon_name: 'edit-delete-symbolic',
            style_class: 'popup-menu-icon',
        });
        // An icon names the button for the eye and for nothing else.
        this.accessible_name = _('Delete');

        this._client = client;
        this.setUuid(uuid);
    }

    setUuid(uuid) {
        this._uuid = uuid;
    }

    vfunc_clicked(_clickedButton) {
        // The button is built before its row's content has been fetched, so it
        // starts with no uuid; ignore a click landing in that window rather than
        // letting GJS throw on the null argument.
        if (this._uuid)
            this._client.delete_item(this._uuid, null);
    }
});
