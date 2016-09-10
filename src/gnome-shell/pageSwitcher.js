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

    _init: function() {
        this.parent({
            reactive: false,
            can_focus: false
        });

        this._box = new St.BoxLayout();
        this.actor.add(this._box, { expand: true, x_fill: false, can_focus: false, reactive: false });

        this._active = -1;
        this._maxDisplayedSize = -1;
        this._pages = 0;

        this.setActive(1);
    },

    setMaxDisplayedSize: function(size) {
        this._maxDisplayedSize = size;
    },

    updateForSize: function(size) {
        const pages = size / this._maxDisplayedSize;

        for (let i = this._pages + 1; i < pages; ++i) {
            this._addPage(i);
        }
        for (let i = pages; i <= this._pages; ++i) {
            this[i].destroy();
        }

        this._pages = pages;

        if (this.getPageOffset() < size) {
            return true;
        } else {
            this._switch(pages);
            return false;
        }
    },

    _addPage: function(page) {
        let sw = new PageItem.GPastePageItem(page);
        this[page] = sw;
        this._box.add(sw.actor, { expand: true, x_fill: false, x_align: St.Align.MIDDLE });

        sw.connect('switch', Lang.bind(this, function(sw, page) {
            this._switch(page);
        }));
    },

    getPageOffset: function() {
        return this._active * this._maxDisplayedSize;
    },

    getPage: function() {
        return this._active + 1;
    },

    setActive: function(page) {
        if (page !== (this._active + 1) && page <= this._pages) {
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
            this._switch(page - 1);
            return Clutter.EVENT_STOP;
        }
        return Clutter.EVENT_PROPAGATE;
    },

    next: function() {
        const page = this.getPage();

        if (page < this._pages) {
            this._switch(page + 1);
            return Clutter.EVENT_STOP;
        }
        return Clutter.EVENT_PROPAGATE;
    },

    _switch: function(page) {
        if (!isNaN(page)) {
            this.emit('switch', page);
        }
    }
});
