// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {Ornament, PopupBaseMenuItem} from 'resource:///org/gnome/shell/ui/popupMenu.js';

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import St from 'gi://St';

import {GPasteAboutItem} from './aboutItem.js';
import {GPastePadding} from './padding.js';
import {GPasteUiItem} from './uiItem.js';

export const GPasteActions = GObject.registerClass(
class GPasteActions extends PopupBaseMenuItem {
    constructor(client, menu, emptyHistoryItem) {
        super({
            reactive: false,
            can_focus: false,
        });

        this.setOrnament(Ornament.NONE);
        // Add padding at the beginning and end so that our contents is centered
        this.add_child(new GPastePadding());
        // A homogeneous box keeps every action button the same width,
        // regardless of how long its label is.
        const box = new St.Widget({
            layout_manager: new Clutter.BoxLayout({homogeneous: true}),
        });
        box.add_child(new GPasteUiItem(menu));
        box.add_child(emptyHistoryItem);
        box.add_child(new GPasteAboutItem(client, menu));
        this.add_child(box);
        this.add_child(new GPastePadding());
    }

    // The menu's Up/Down navigation lands on one of our buttons, but they sit
    // side by side, so move between them with Left/Right ourselves: the focused
    // button's key press bubbles up here, and navigate_focus walks our children.
    vfunc_key_press_event(event) {
        const symbol = event.get_key_symbol();
        if (symbol === Clutter.KEY_Left || symbol === Clutter.KEY_Right) {
            const direction = symbol === Clutter.KEY_Left
                ? St.DirectionType.LEFT
                : St.DirectionType.RIGHT;
            if (this.navigate_focus(global.stage.key_focus, direction, false))
                return Clutter.EVENT_STOP;
        }
        return super.vfunc_key_press_event(event);
    }
});
