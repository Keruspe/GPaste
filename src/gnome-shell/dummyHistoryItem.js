// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';
import {PopupMenuItem} from 'resource:///org/gnome/shell/ui/popupMenu.js';

import GObject from 'gi://GObject';

export const GPasteDummyHistoryItem = GObject.registerClass(
class GPasteDummyHistoryItem extends PopupMenuItem {
    constructor() {
        super(_("(Couldn't connect to GPaste daemon)"));
        this.setSensitive(false);
    }

    showEmpty() {
        this.label.text = _('(Empty)');
        this.show();
    }

    showNoResult() {
        this.label.text = _('(No result)');
        this.show();
    }
});
