// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

// Execute the shipped search item over St doubles: what its Pinned filter's
// star shows as the filter goes on and off, not Shell rendering.
const assert = require('node:assert/strict');
const vm = require('node:vm');
const {Signals, loadShipped} = require('./suite.cjs');

// What the shipped item asks of its children: a toggle button raising
// notify::checked when its state moves, as St.Button does, and an entry whose
// text and cursor it may set.
class Button extends Signals {
    constructor(params) {
        super();
        Object.assign(this, params);
        this._checked = false;
    }

    get checked() { return this._checked; }
    set checked(checked) {
        if (this._checked === checked)
            return;
        this._checked = checked;
        this.emit('notify::checked');
    }
}

class Entry extends Signals {
    constructor(params) {
        super();
        Object.assign(this, params);
        this.text = '';
        this.clutter_text = new (class extends Signals {
            set_cursor_visible() {}
            set_selection() {}
        })();
    }

    get_text() { return this.text; }
    set_primary_icon() {}
    set_secondary_icon() {}
}

class PopupBaseMenuItem extends Signals {
    add_child(child) { (this.children ??= []).push(child); }
}

const context = vm.createContext({
    _: value => value,
    GObject: {registerClass: (...args) => args.at(-1)},
    PopupBaseMenuItem,
    St: {Button, Entry, Icon: class { constructor(params) { Object.assign(this, params); } }},
});
loadShipped(context, {SearchItem: 'GPasteSearchItem'});

// The Pinned filter's star is drawn as the rows' are: empty while the filter
// is off, full while it shows only the pinned items, and empty again once a
// reset takes the filter off.
{
    const item = new context.SearchItem();
    const star = item._favouritesButton.child;

    assert.equal(star.icon_name, 'non-starred-symbolic');
    item._favouritesButton.checked = true;
    assert.equal(item.favourites, true);
    assert.equal(star.icon_name, 'starred-symbolic');
    item._favouritesButton.checked = false;
    assert.equal(star.icon_name, 'non-starred-symbolic');
    item._favouritesButton.checked = true;
    item.reset();
    assert.equal(item.favourites, false);
    assert.equal(star.icon_name, 'non-starred-symbolic');
}
