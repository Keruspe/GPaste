/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const PopupMenu = imports.ui.popupMenu;

const St = imports.gi.St;

var GPasteStatusIcon = class {
    constructor() {
        this.actor = new St.BoxLayout({ style_class: 'panel-status-menu-box' });

        this.actor.add_child(new St.Icon({
            icon_name: 'edit-paste-symbolic',
            style_class: 'system-status-icon'
        }));

        this.actor.add_child(PopupMenu.arrowIcon(St.Side.BOTTOM));
    }
};
