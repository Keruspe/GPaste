/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const PopupMenu = imports.ui.popupMenu;

const { GObject, St } = imports.gi;

var GPasteStatusIcon = GObject.registerClass(
class GPasteStatusIcon extends St.BoxLayout {
    _init() {
        super._init({ style_class: 'panel-status-menu-box' });

        this.add_child(new St.Icon({
            icon_name: 'edit-paste-symbolic',
            style_class: 'system-status-icon'
        }));

        this.add_child(PopupMenu.arrowIcon(St.Side.BOTTOM));
    }
});
