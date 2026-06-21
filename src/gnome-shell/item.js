// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {PopupMenuItem} from 'resource:///org/gnome/shell/ui/popupMenu.js';

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import Pango from 'gi://Pango';
import St from 'gi://St';

import {GPasteDeleteItemPart} from './deleteItemPart.js';

export const GPasteItem = GObject.registerClass(
class GPasteItem extends PopupMenuItem {
    constructor(client, size, slotIndex, index, uuid = null) {
        // hover: false keeps the pointer from stealing key focus from the search
        // entry (Fix #435) without dropping can_focus, so the rows stay reachable
        // with the arrow keys.
        super('', {hover: false});
        this.label.set_x_expand(true);

        this._client = client;
        this._index = -1;
        this._uuid = null;
        this._generation = 0;
        // The full (untruncated) text currently set on the label. Compared
        // against in _setValue to skip redundant set_text() calls; we can't use
        // label.get_text() for that because max_length truncates what the label
        // stores, so a long value would never compare equal.
        this._displayedText = null;

        if (slotIndex <= 9) {
            this._indexLabel = new St.Label({
                text: `${slotIndex}: `,
            });
            this._indexLabelVisible = false;
        }

        this._deleteItem = new GPasteDeleteItemPart(client, this._uuid);
        this.add_child(this._deleteItem);

        this.label.clutter_text.ellipsize = Pango.EllipsizeMode.END;
        this.setTextSize(size);

        // Search rows are addressed by uuid (the search returns uuids); history
        // rows by their index.
        if (uuid !== null)
            this.setUuid(uuid).catch(console.error);
        else
            this.setIndex(index).catch(console.error);
    }

    destroy() {
        // Discard any in-flight setIndex()/setUuid() fetch: bumping the
        // generation makes its post-await guard bail out instead of touching
        // this now-finalized actor.
        this._generation++;
        super.destroy();
    }

    showIndex(state) {
        if (state) {
            if (!this._indexLabelVisible)
                this.insert_child_at_index(this._indexLabel, 1);
        } else if (this._indexLabelVisible) {
            this.remove_child(this._indexLabel);
        }
        this._indexLabelVisible = state;
    }

    refresh() {
        this.setIndex(this._index).catch(console.error);
    }

    async setIndex(index) {
        const generation = ++this._generation;
        this._index = index;

        if (index === -1) {
            this._setValue(null);
        } else {
            const item = await this._client.get_element_at_index(index);
            if (generation !== this._generation)
                return;
            this._uuid = item.get_uuid();
            this._setValue(item.get_value());
        }
    }

    async setUuid(uuid) {
        const generation = ++this._generation;
        this._index = -2;
        this._uuid = uuid;

        if (uuid == null) {
            this._setValue(null);
        } else {
            const value = await this._client.get_element(uuid);
            if (generation !== this._generation)
                return;
            this._setValue(value);
        }
    }

    _setValue(value) {
        this.label.set_style(this._index === 0 ? 'font-weight: bold;' : null);

        if (this._index === -1) {
            this._uuid = null;
            this._displayedText = value || '';
            this.label.clutter_text.set_text(this._displayedText);
            this.hide();
        } else {
            const text = (value ?? '').replace(/[\t\n\r]/g, ' ');
            if (text !== this._displayedText) {
                this._displayedText = text;
                this.label.clutter_text.set_text(text);
            }

            this.show();
        }

        this._deleteItem.setUuid(this._uuid);
    }

    setTextSize(size) {
        this.label.clutter_text.max_length = size;
    }

    activate(event) {
        this._client.select(this._uuid, null);
        super.activate(event);
    }

    vfunc_key_press_event(event) {
        const symbol = event.get_key_symbol();
        if (symbol === Clutter.KEY_space || symbol === Clutter.KEY_Return) {
            this.activate(event);
            return Clutter.EVENT_STOP;
        }
        if (symbol === Clutter.KEY_BackSpace || symbol === Clutter.KEY_Delete) {
            this._client.delete(this._uuid, null);
            return Clutter.EVENT_STOP;
        }
        // Chain up so PopupBaseMenuItem keeps handling arrow-key focus
        // navigation between history rows.
        return super.vfunc_key_press_event(event);
    }
});
