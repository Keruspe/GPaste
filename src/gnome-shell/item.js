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
    constructor(client, size, slotIndex, index) {
        super('', {can_focus: false});
        this.label.set_x_expand(true);

        this._client = client;
        this._index = -1;
        this._fakeIndex = false;
        this._uuid = null;
        this._generation = 0;

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

        this.setIndex(index).catch(console.error);
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
        const oldIndex = this._index;
        this._index = index;
        this._fakeIndex = false;

        if (index === -1) {
            this._setValue(null, oldIndex);
        } else {
            const item = await this._client.get_element_at_index(index, null);
            if (generation !== this._generation)
                return;
            this._uuid = item.get_uuid();
            this._setValue(item.get_value(), oldIndex);
        }
    }

    async setUuid(uuid) {
        const generation = ++this._generation;
        const oldIndex = this._index;
        this._index = -2;
        this._fakeIndex = true;
        this._uuid = uuid;

        if (uuid == null) {
            this._setValue(null, oldIndex);
        } else {
            const value = await this._client.get_element(uuid, null);
            if (generation !== this._generation)
                return;
            this._setValue(value, oldIndex);
        }
    }

    _setValue(value, oldIndex) {
        if (this._index === 0)
            this.label.set_style('font-weight: bold;');
        else if (oldIndex === 0)
            this.label.set_style(null);


        if (this._index === -1) {
            this._uuid = null;
            this.label.clutter_text.set_text(value || '');
            this.hide();
        } else {
            const text = (value ?? '').replace(/[\t\n\r]/g, ' ');
            if (text !== this.label.get_text())
                this.label.clutter_text.set_text(text);

            if (oldIndex === -1)
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
        return Clutter.EVENT_PROPAGATE;
    }
});
