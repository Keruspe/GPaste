// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';
import {PopupBaseMenuItem} from 'resource:///org/gnome/shell/ui/popupMenu.js';

import GObject from 'gi://GObject';
import St from 'gi://St';

export const GPasteSearchItem = GObject.registerClass({
    Signals: {
        'text-changed': {param_types: []},
        'favourites-changed': {param_types: []},
    },
}, class GPasteSearchItem extends PopupBaseMenuItem {
    constructor() {
        super({
            activate: false,
            reactive: true,
            hover: false,
            can_focus: false,
        });

        this._entry = new St.Entry({
            name: 'GPasteSearchEntry',
            style_class: 'search-entry',
            hint_text: _('Type to search…'),
            track_hover: true,
            reactive: true,
            can_focus: true,
        });
        this.add_child(this._entry);

        this._entry.set_primary_icon(new St.Icon({
            style_class: 'search-entry-icon',
            icon_name: 'edit-find-symbolic',
        }));
        this._entry.clutter_text.connect('text-changed', this._onTextChanged.bind(this));

        // The filter sits with the search because it is the same kind of thing:
        // both narrow the list down to rows named rather than counted, and they
        // compose (a search within the pinned items).
        this._favourites = false;
        // Set while reset() drops both filters at once, so neither asks the
        // caller for a reload of its own: it makes the one reload both need.
        this._silent = false;
        this._favouritesButton = new St.Button({
            child: new St.Icon({
                icon_name: 'starred-symbolic',
                style_class: 'popup-menu-icon',
            }),
            toggle_mode: true,
            can_focus: true,
        });
        this._favouritesButton.connect('notify::checked', () => {
            this._favourites = this._favouritesButton.checked;
            if (!this._silent)
                this.emit('favourites-changed');
        });
        this.add_child(this._favouritesButton);

        this._clearIcon = new St.Icon({
            style_class: 'search-entry-icon',
            icon_name: 'edit-clear-symbolic',
        });
        this._iconClickedId = 0;
    }

    get text() {
        return this._entry.get_text();
    }

    get favourites() {
        return this._favourites;
    }

    resetSize(size) {
        this._entry.style = `width: ${size}em`;
    }

    // Both filters, not just the typed one: the menu is reset so it opens
    // showing the history, and a star left latched from last time would open it
    // on a pinned-only list with an empty search box and nothing saying why.
    // Silently, because each filter would otherwise announce itself and cost a
    // reload apiece where the caller makes one for the pair.
    reset() {
        this._silent = true;
        this._clearEntry();
        this._favouritesButton.checked = false;
        this._silent = false;
    }

    // The entry's own clear icon clears the entry. The star is a filter of its
    // own: taking a pinned-only list away because the query it was narrowing
    // went away would drop something the user never asked to drop.
    _clearEntry() {
        this._entry.text = '';
        const text = this._entry.clutter_text;
        text.set_cursor_visible(true);
        text.set_selection(0, 0);
    }

    grabFocus() {
        this._entry.grab_key_focus();
    }

    _onTextChanged() {
        const dummy = this.text.length === 0;
        this._entry.set_secondary_icon(dummy ? null : this._clearIcon);
        if (!dummy && this._iconClickedId === 0)
            this._iconClickedId = this._entry.connect('secondary-icon-clicked', this._clearEntry.bind(this));

        if (!this._silent)
            this.emit('text-changed');
    }
});
