// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';
import {PopupMenuItem} from 'resource:///org/gnome/shell/ui/popupMenu.js';

import GObject from 'gi://GObject';

export const GPasteDummyHistoryItem = GObject.registerClass(
class GPasteDummyHistoryItem extends PopupMenuItem {
    // The menu can be opened while the client is still being connected -- with
    // retries, that is a few seconds after login -- so it opens saying it is
    // busy, not that it failed. Only _connect() giving up says that.
    constructor() {
        super(_('Loading…'));
        this.setSensitive(false);
    }

    showDisconnected() {
        this.label.text = _('GPaste daemon not running');
        this.show();
    }

    showEmpty() {
        this.label.text = _('No Items');
        this.show();
    }

    showNoResult() {
        this.label.text = _('No Results');
        this.show();
    }

    // Distinct from the above: nothing pinned is not a search that matched
    // nothing, and telling a user their search came up empty when they never
    // searched leaves them with no idea what to do about it.
    showNoPinned() {
        this.label.text = _('No Pinned Items');
        this.show();
    }
});
