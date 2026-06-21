// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {Ornament, PopupBaseMenuItem} from 'resource:///org/gnome/shell/ui/popupMenu.js';

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import St from 'gi://St';

import {GPasteAboutItem} from './aboutItem.js';
import {GPasteUiItem} from './uiItem.js';

export const GPasteActions = GObject.registerClass(
class GPasteActions extends PopupBaseMenuItem {
    constructor(client, menu, emptyHistoryItem) {
        super({
            reactive: false,
            can_focus: false,
        });

        this.setOrnament(Ornament.NONE);
        // A homogeneous box keeps every action button the same width, regardless
        // of how long its label is; centering it keeps the row balanced.
        const box = new St.Widget({
            x_expand: true,
            x_align: Clutter.ActorAlign.CENTER,
            layout_manager: new Clutter.BoxLayout({homogeneous: true}),
        });
        box.add_child(new GPasteUiItem(menu));
        box.add_child(emptyHistoryItem);
        box.add_child(new GPasteAboutItem(client, menu));
        this.add_child(box);

        // Left/Right navigation between these buttons is driven from the
        // indicator's _onMenuKeyPress: this row is non-reactive, so the key
        // event never bubbles through it, only through the (reactive) menu actor.
    }
});
