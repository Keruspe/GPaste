/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2023, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import { gettext as _ } from 'resource:///org/gnome/shell/extensions/extension.js';
import { PopupMenuItem } from 'resource:///org/gnome/shell/ui/popupMenu.js';

import GObject from 'gi://GObject';

export const GPasteDummyHistoryItem = GObject.registerClass(
class GPasteDummyHistoryItem extends PopupMenuItem {
    _init() {
        super._init(_("(Couldn't connect to GPaste daemon)"));
        this.setSensitive(false);
    }

    showEmpty() {
        this.label.text = _("(Empty)");
        this.show();
    }

    showNoResult() {
        this.label.text = _("(No result)");
        this.show();
    }
});
