/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2023, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import { PopupBaseMenuItem } from 'resource:///org/gnome/shell/ui/popupMenu.js';

import GObject from 'gi://GObject';
import St from 'gi://St';

export const GPasteSearchItem = GObject.registerClass({
    Signals: {
        'text-changed': { param_types: [] },
    },
}, class GPasteSearchItem extends PopupBaseMenuItem {
    _init() {
        super._init({
            activate: false,
            reactive: true,
            hover: false,
            can_focus: false
        });

        this._entry = new St.Entry({
            name: 'GPasteSearchEntry',
            style_class:'search-entry',
            track_hover: true,
            reactive: true,
            can_focus: true
        });
        this.add_child(this._entry);

        this._entry.set_primary_icon(new St.Icon({
            style_class:'search-entry-icon',
            icon_name:'edit-find-symbolic'
        }));
        this._entry.clutter_text.connect('text-changed', this._onTextChanged.bind(this));

        this._clearIcon = new St.Icon({
            style_class: 'search-entry-icon',
            icon_name: 'edit-clear-symbolic'
        });
        this._iconClickedId = 0;
    }

    get text() {
        return this._entry.get_text();
    }

    resetSize(size) {
        this._entry.style = 'width: ' + size + 'em';
    }

    reset() {
        this._entry.text = '';
        let text = this._entry.clutter_text;
        text.set_cursor_visible(true);
        text.set_selection(0, 0);
    }

    grabFocus() {
        this._entry.grab_key_focus();
    }

    _onTextChanged(se, prop) {
        const dummy = (this.text.length == 0);
        this._entry.set_secondary_icon((dummy) ? null : this._clearIcon);
        if (!dummy && this._iconClickedId == 0) {
            this._iconClickedId = this._entry.connect('secondary-icon-clicked', this.reset.bind(this));
        }
        this.emit('text-changed');
    }
});
