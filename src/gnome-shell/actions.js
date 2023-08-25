/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2023, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import { Ornament, PopupBaseMenuItem } from 'resource:///org/gnome/shell/ui/popupMenu.js';

import GObject from 'gi://GObject';

import { GPasteAboutItem } from './aboutItem.js';
import { GPastePadding } from './padding.js';
import { GPasteUiItem } from './uiItem.js';

export const GPasteActions = GObject.registerClass(
class GPasteActions extends PopupBaseMenuItem {
    _init(client, menu, emptyHistoryItem) {
        super._init({
            reactive: false,
            can_focus: false
        });

        this.setOrnament(Ornament.NONE);
        // Add padding at the beginning and end so that our contents is centered
        this.add_child(new GPastePadding());
        this.add_child(new GPasteUiItem(menu));
        this.add_child(emptyHistoryItem);
        this.add_child(new GPasteAboutItem(client, menu));
        this.add_child(new GPastePadding());
    }
});
