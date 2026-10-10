// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

// Execute the shipped tracking switch over a PopupSwitchMenuItem double: what a
// toggle asks of the daemon and what the switch shows once it has answered,
// not Shell rendering.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const errors = [];

// What the real item (GNOME Shell's popupMenu.js) does: activate() calls
// toggle(), which flips the switch, and setToggleState() sets it; 'toggled'
// comes from the switch's notify::state, which a state left as it was does not
// raise. A destroyed actor takes no state at all.
class PopupSwitchMenuItem {
    constructor(_text, active) {
        this._state = active;
        this._handlers = [];
    }

    connect(signal, handler) { this._handlers.push({signal, handler}); }
    emit(signal, ...args) {
        for (const {signal: name, handler} of this._handlers) {
            if (name === signal)
                handler(this, ...args);
        }
    }

    get state() { return this._state; }
    activate() { this.toggle(); }
    toggle() { this._setSwitch(!this._state); }
    setToggleState(state) { this._setSwitch(state); }
    _setSwitch(state) {
        assert.ok(!this.gone, 'a destroyed switch was set');
        if (this._state === state)
            return;
        this._state = state;
        this.emit('toggled', state);
    }

    destroy() {
        this.emit('destroy');
        this.gone = true;
    }
}

const context = vm.createContext({
    console: {error: message => errors.push(message)},
    _: value => value,
    GObject: {registerClass: (...args) => args.at(-1)},
    PopupSwitchMenuItem,
});
const source = fs.readFileSync(process.argv[2], 'utf8')
    .replace(/^import [^;]*;\n/gm, '').replace('export const ', 'const ');
vm.runInContext(`${source}\nthis.StateSwitch = GPasteStateSwitch;`, context);

// The daemon side of SetActive: it answers once the test says so, refusing
// when its track-changes setting cannot be written. A call made without a
// callback drops the answer, as GJS does.
function client(active, refuse) {
    return {
        active,
        calls: [],
        is_active() { return this.active; },
        set_active(state, _cancellable, callback) {
            this.calls.push(state);
            this.answer = () => callback?.(this, state);
        },
        set_active_finish(state) {
            if (refuse)
                throw new Error('the track-changes setting cannot be changed');
            this.active = state;
        },
    };
}

// A refusal puts the switch back where the daemon still is, without asking
// the daemon again on the way.
{
    errors.length = 0;
    const daemon = client(true, true);
    const item = new context.StateSwitch(daemon);

    item.activate();
    assert.equal(item.state, false);
    daemon.answer();
    assert.equal(item.state, true);
    assert.deepEqual(daemon.calls, [false]);
    assert.equal(errors.length, 1);
}

// An accepted call leaves the switch as the click put it.
{
    errors.length = 0;
    const daemon = client(true, false);
    const item = new context.StateSwitch(daemon);

    item.activate();
    daemon.answer();
    assert.equal(item.state, false);
    assert.equal(daemon.active, false);
    assert.deepEqual(daemon.calls, [false]);
    assert.equal(errors.length, 0);
}

// A refusal reaching a switch the menu has destroyed is only logged.
{
    errors.length = 0;
    const daemon = client(false, true);
    const item = new context.StateSwitch(daemon);

    item.activate();
    item.destroy();
    daemon.answer();
    assert.equal(errors.length, 1);
}

// The daemon moving the switch is not the user asking it to: nothing is asked
// back of the daemon.
{
    errors.length = 0;
    const daemon = client(true, false);
    const item = new context.StateSwitch(daemon);

    item.syncState(false);
    assert.equal(item.state, false);
    assert.deepEqual(daemon.calls, []);
    assert.equal(errors.length, 0);
}
