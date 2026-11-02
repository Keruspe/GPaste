// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

// Execute the shipped item actions row with doubles for St and the menu item:
// that the keyboard reaching one of its buttons scrolls the row into view. Not
// Shell rendering.
const assert = require('node:assert/strict');
const vm = require('node:vm');
const {Signals, loadShipped} = require('./suite.cjs');

const revealed = [];

class PopupBaseMenuItem extends Signals {
    add_child() {}
}

class Button extends Signals {
    grab_key_focus() { this.emit('key-focus-in'); }
}

const buttons = [];
const context = vm.createContext({
    _: text => text,
    PopupBaseMenuItem,
    GObject: {registerClass: (meta, klass) => klass, TYPE_STRING: 0},
    St: {BoxLayout: class {}},
    addActionButton: (box, label, action) => {
        const button = new Button();

        button.label = label;
        button.connect('clicked', action);
        buttons.push(button);
        return button;
    },
    ensureActorVisibleInScrollView: (scrollView, actor) => revealed.push({scrollView, actor}),
});
loadShipped(context, {ItemActions: 'GPasteItemActionsItem'});

const scrollView = {};
const actions = new context.ItemActions(scrollView);

assert.deepEqual(buttons.map(button => button.label), ['Remove Rich Text', 'Upload']);
for (const button of buttons) {
    revealed.length = 0;
    button.grab_key_focus();
    assert.equal(revealed.length, 1);
    assert.equal(revealed[0].scrollView, scrollView);
    assert.equal(revealed[0].actor, actions);
}

// What the buttons act on is the item the row was opened for.
const emitted = [];
actions.connect('strip-rich-text', (item, uuid) => emitted.push(['strip-rich-text', uuid]));
actions.connect('upload', (item, uuid) => emitted.push(['upload', uuid]));
actions.reset('a');
for (const button of buttons)
    button.emit('clicked');
assert.deepEqual(emitted, [['strip-rich-text', 'a'], ['upload', 'a']]);

console.log('Item actions: scrolling into view and the item acted on passed');
