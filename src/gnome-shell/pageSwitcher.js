/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Lang = imports.lang;

const PopupMenu = imports.ui.popupMenu;

const St = imports.gi.St;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();

const PageItem = Me.imports.pageItem;

const GPastePageSwitcher = new Lang.Class({
    Name: 'GPastePageSwitcher',
    Extends: PopupMenu.PopupBaseMenuItem,

    _init: function(limit, indicator) {
        this.parent({
            reactive: false,
            can_focus: false
        });

        this._box = new St.BoxLayout();
        this.actor.add(this._box, { expand: true, x_fill: false });

        for (let i = 0; i < limit; ++i) {
            let sw = new PageItem.GPastePageItem(-1);
            this[i] = sw;
            this._box.add(sw.actor, { expand: true, x_fill: false, x_align: St.Align.MIDDLE });

            sw.actor.connect('switch', Lang.bind(this, function(page) {
                this.actor.emit('switch', page);
            }));
        }
    }
});
