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
        this._pages = [];

        this.setActive(1);
    },

    setMaxDisplayedSize: function(size) {
        this._maxDisplayedSize = size;
    },

    updateForSize: function(size) {
        const pages = (size === 0) ? 0 : Math.floor(size / this._maxDisplayedSize + 1);

        for (let i = this._pages.length; i < pages; ++i) {
            this._addPage();
        }
        while (this._pages.length !== pages) {
            this._pages.pop().destroy();
        }

        if (this.getPageOffset() < size) {
            return true;
        } else {
            this._switch(pages);
            return false;
        }
    },

    _addPage: function() {
        let sw = new PageItem.GPastePageItem(this._pages.length + 1);
        this._pages.push(sw);
        this._box.add(sw.actor, { expand: true, x_fill: false, x_align: St.Align.MIDDLE });

        sw.connect('switch', Lang.bind(this, function(sw, page) {
            this._switch(page);
        }));
    },

    getPageOffset: function() {
        return (this._active < 0) ? 0 : (this._active * this._maxDisplayedSize);
    },

    getPage: function() {
        return this._active + 1;
    },

    setActive: function(page) {
        if (page !== (this._active + 1) && page <= this._pages.length) {
            if (this._active !== -1) {
                this._pages[this._active].setActive(false);
            }
            this._active = page - 1;
            this._pages[this._active].setActive(true);
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

        if (page < this._pages.length) {
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
