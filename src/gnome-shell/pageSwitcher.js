/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Lang = imports.lang;

const PopupMenu = imports.ui.popupMenu;

const Clutter = imports.gi.Clutter;
const St = imports.gi.St;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();

const PageItem = Me.imports.pageItem;

const GPastePageSwitcher = new Lang.Class({
    Name: 'GPastePageSwitcher',
    Extends: PopupMenu.PopupBaseMenuItem,

    _init: function(limit) {
        this.parent({
            reactive: false,
            can_focus: false
        });

        this._box = new St.BoxLayout();
        this.actor.add(this._box, { expand: true, x_fill: false, can_focus: false, reactive: false });

        this._active = -1;
        this._maxDisplayedSize = -1;
        this._pages = limit;

        for (let i = 0; i < limit; ++i) {
            let sw = new PageItem.GPastePageItem(-1);
            this[i] = sw;
            this._box.add(sw.actor, { expand: true, x_fill: false, x_align: St.Align.MIDDLE });

            sw.connect('switch', Lang.bind(this, function(sw, page) {
                if (!isNaN(page)) {
                    this.emit('switch', page);
                }
            }));
        }

        this.setActive(1);
    },

    setMaxDisplayedSize: function(size) {
        this._maxDisplayedSize = size;
    },

    getPageOffset: function() {
        return this._active * this._maxDisplayedSize;
    },

    getPage: function() {
        return this._active + 1;
    },

    setActive: function(page) {
        if (page !== (this._active + 1)) {
            if (this._active !== -1) {
                this[this._active].setActive(false);
            }
            this._active = page - 1;
            this[this._active].setActive(true);
        }
    },

    previous: function() {
        const page = this.getPage();

        if (page > 1) {
            this.setAtive(page - 1);
            return Clutter.EVENT_STOP;
        }
        return Clutter.EVENT_PROPAGATE;
    },

    next: function() {
        const page = this.getPage();

        if (page < this._pages) {
            this.setAtive(page + 1);
            return Clutter.EVENT_STOP;
        }
        return Clutter.EVENT_PROPAGATE;
    }
});
