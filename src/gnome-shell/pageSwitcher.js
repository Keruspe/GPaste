/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const PopupMenu = imports.ui.popupMenu;

const { Clutter, St } = imports.gi;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();

const PageItem = Me.imports.pageItem;

const MAX_PAGES = 20;

class GPastePageSwitcher extends PopupMenu.PopupBaseMenuItem {
    constructor() {
        super({
            reactive: false,
            can_focus: false
        });

        this._box = new St.BoxLayout();
        this.actor.add(this._box, { expand: true, x_fill: false, can_focus: false, reactive: false });

        this._active = -1;
        this._maxDisplayedSize = -1;
        this._pages = [];
    }

    setMaxDisplayedSize(size) {
        this._maxDisplayedSize = size;
    }

    updateForSize(size) {
        const pages = Math.min((size === 0) ? 0 : Math.floor((size - 1) / this._maxDisplayedSize + 1), MAX_PAGES);

        for (let i = this._pages.length; i < pages; ++i) {
            this._addPage();
        }
        while (this._pages.length !== pages) {
            this._pages.pop().destroy();
        }

        if (size > 0 && this._active === -1) {
            this._switch(1);
            return false;
        } else if (size == 0 || this._active < pages) {
            return true;
        } else {
            this._active = -1;
            this._switch(pages);
            return false;
        }
    }

    _addPage() {
        let sw = new PageItem.GPastePageItem(this._pages.length + 1);
        this._pages.push(sw);
        this._box.add(sw.actor, { expand: true, x_fill: false, x_align: St.Align.MIDDLE });

        sw.connect('switch', (sw, page) => {
            this._switch(page);
        });
    }

    getPageOffset() {
        return (this._active < 0) ? 0 : (this._active * this._maxDisplayedSize);
    }

    getPage() {
        return this._active + 1;
    }

    setActive(page) {
        if (page !== 0 && page !== (this._active + 1) && page <= this._pages.length) {
            if (this._active !== -1) {
                this._pages[this._active].setActive(false);
            }
            this._active = page - 1;
            this._pages[this._active].setActive(true);
        }
    }

    previous() {
        const page = this.getPage();

        if (page > 1) {
            this._switch(page - 1);
            return Clutter.EVENT_STOP;
        }
        return Clutter.EVENT_PROPAGATE;
    }

    next() {
        const page = this.getPage();

        if (page < this._pages.length) {
            this._switch(page + 1);
            return Clutter.EVENT_STOP;
        }
        return Clutter.EVENT_PROPAGATE;
    }

    _switch(page) {
        if (!isNaN(page)) {
            this.emit('switch', page);
        }
    }
};
