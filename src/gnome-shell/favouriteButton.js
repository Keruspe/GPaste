// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import GObject from 'gi://GObject';
import St from 'gi://St';

// The star says what the item is, not what the button does: its icon is the
// state, and clicking asks the daemon for the other one. Nothing is set here —
// the daemon answers with an update, which refills the row and repaints the
// icon — so a refused call simply leaves the star as it was.
export const GPasteFavouriteButton = GObject.registerClass(
class GPasteFavouriteButton extends St.Button {
    constructor(client, uuid) {
        super();

        this._icon = new St.Icon({
            icon_name: 'non-starred-symbolic',
            style_class: 'popup-menu-icon',
        });
        this.child = this._icon;

        this._client = client;
        this._favourite = false;
        this.setUuid(uuid);
    }

    setUuid(uuid) {
        this._uuid = uuid;
    }

    setFavourite(favourite) {
        this._favourite = favourite;
        this._icon.icon_name = favourite ? 'starred-symbolic' : 'non-starred-symbolic';
    }

    vfunc_clicked(_clickedButton) {
        // The button is built before its row's content has been fetched, so it
        // starts with no uuid; ignore a click landing in that window rather than
        // letting GJS throw on the null argument.
        if (this._uuid)
            this._client.set_favourite(this._uuid, !this._favourite, null);
    }
});
