// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';
import {PopupMenuItem} from 'resource:///org/gnome/shell/ui/popupMenu.js';

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import St from 'gi://St';

export const GPasteDummyHistoryItem = GObject.registerClass({
    // Activating a menu item closes the menu: PopupMenuBase listens for
    // 'activate' on every item it holds and dismisses on it. That would take
    // away the very row about to report what the retry did, so this one says it
    // its own way and the indicator listens for that instead.
    Signals: {'retry': {}},
}, class GPasteDummyHistoryItem extends PopupMenuItem {
    constructor() {
        super('');

        // The offer is a label of its own rather than a few words appended to
        // the state's: the two are not one sentence -- one says what happened,
        // the other what activating the row does -- and a translation of
        // "GPaste daemon not running" stops being one the moment something else
        // is stuck on its end, which is how the translations it had were lost.
        // Not a button either: the row itself is what activates, and a second
        // focus target inside it would take over the keyboard navigation a menu
        // item gives us for free.
        this.label.set_x_expand(true);
        this._retry = new St.Label({
            text: _('Retry'),
            x_align: Clutter.ActorAlign.END,
            y_align: Clutter.ActorAlign.CENTER,
        });
        this.add_child(this._retry);

        this.showLoading();
    }

    // What a click and the Enter key both reach: the base class emits
    // 'activate' from here, and not emitting it is what keeps the menu open.
    activate() {
        this.emit('retry');
    }

    // Sensitivity is what makes a menu item reactive and reachable by the
    // keyboard, so it is also what tells the states apart: every one of these
    // is a label the user reads, except the disconnected one, which is the way
    // to ask for a daemon and says so.
    _showState(text, activatable) {
        this.label.text = text;
        // Being an offer and being reactive are the same state, so the one
        // label that says so follows the sensitivity rather than each caller.
        this._retry.visible = activatable;
        this.setSensitive(activatable);
        this.show();
    }

    // The menu can be opened while the client is still being connected -- with
    // retries, that is a few seconds after login -- and a daemon that owns the
    // bus name but has not answered yet is starting rather than missing. Both
    // say the same thing: busy, not failed.
    showLoading() {
        this._showState(_('Loading…'), false);
    }

    showDisconnected() {
        this._showState(_('GPaste daemon not running'), true);
    }

    showEmpty() {
        this._showState(_('No Items'), false);
    }

    showNoResult() {
        this._showState(_('No Results'), false);
    }

    // Distinct from the above: nothing pinned is not a search that matched
    // nothing, and telling a user their search came up empty when they never
    // searched leaves them with no idea what to do about it.
    showNoPinned() {
        this._showState(_('No Pinned Items'), false);
    }
});
