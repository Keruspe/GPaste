// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

// Execute the shipped history row with doubles for St, Clutter and the client:
// what it shows of a long item, what it acts on while a new one is on its way,
// where the menu key reaches it, and what a failed read leaves in the journal.
// Not Shell rendering.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const errors = [];
const debugged = [];

// Signals as the shell's actors carry them, enough to emit one by hand.
class Signals {
    connect(signal, handler) {
        (this._handlers ??= []).push({signal, handler});
        return this._handlers.length;
    }

    emit(signal, ...args) {
        for (const {signal: name, handler} of this._handlers ?? []) {
            if (name === signal)
                handler(this, ...args);
        }
    }
}

class Text {
    set_text(text) { this.text = text; }
}

class PopupMenuItem extends Signals {
    static get_binding_pool() { return {install_closure() {}}; }

    constructor() {
        super();
        this.label = {clutter_text: new Text(), set_x_expand() {}, set_style() {}, get_preferred_height: () => [0, 16]};
    }

    insert_child_below() {}
    add_child() {}
    add_action() {}
    remove_child() {}
    show() { this.visible = true; }
    hide() { this.visible = false; }
    contains() { return false; }
    activate() { this.activated = true; }
    destroy() {}
}

class Button extends Signals {
    setUuid(uuid) { this.uuid = uuid; }
    setFavourite() {}
}

const context = vm.createContext({
    console: {error: e => errors.push(e), debug: message => debugged.push(message)},
    PopupMenuItem,
    GObject: {registerClass: (meta, klass) => klass, TYPE_STRING: 0, TYPE_BOOLEAN: 1},
    Clutter: {
        KEY_BackSpace: 0, KEY_Delete: 1, EVENT_STOP: true, BUTTON_SECONDARY: 3,
        ActorAlign: {CENTER: 0},
        ClickGesture: class extends Signals {},
    },
    St: {Label: class {}, Bin: class {}, Icon: class {}, Widget: class {}},
    Pango: {EllipsizeMode: {END: 3}},
    Gio: {BytesIcon: {new: () => null}},
    GPaste: {
        ItemKind: {TEXT: 0, IMAGE: 1, COLOR: 2},
        util_one_line: text => text,
        util_display_string: value => value,
    },
    global: {stage: {connect: () => 1, disconnect() {}, get_key_focus: () => null}},
    GPasteDeleteButton: Button,
    GPasteFavouriteButton: Button,
    replaceCancellable: previous => {
        previous?.cancel();
        return {cancel() { this.cancelled = true; }, is_cancelled() { return !!this.cancelled; }};
    },
    awaitReply: async (cancellable, call) => {
        const reply = await call;
        return [!cancellable.is_cancelled(), reply];
    },
    logFailure: finish => ({finish}),
    debugFailure: e => debugged.push(e.message),
});
const source = fs.readFileSync(process.argv[2], 'utf8')
    .replace(/^import [^;]*;\n/gm, '').replace('export class ', 'class ');
vm.runInContext(`${source}\nthis.Item = GPasteItem;`, context);

function item(uuid, value) {
    return {get_uuid: () => uuid, get_value: () => value, is_favourite: () => false, get_kind: () => 0};
}

// A client whose items the test hands out, and which records what is sent.
function client() {
    return {
        sent: [],
        pending: null,
        get_item_at_index(index, cancellable) {
            this.cancellable = cancellable;
            return new Promise((resolve, reject) => { this.pending = {resolve, reject}; });
        },
        get_item(uuid, cancellable) {
            this.cancellable = cancellable;
            return new Promise((resolve, reject) => { this.pending = {resolve, reject}; });
        },
        select(uuid, cancellable, callback) { this.sent.push({method: 'select', uuid, callback}); },
        delete_item(uuid, cancellable, callback) { this.sent.push({method: 'delete_item', uuid, callback}); },
    };
}

async function filled(row, bus, uuid, value) {
    bus.pending.resolve(item(uuid, value));
    await new Promise(resolve => setImmediate(resolve));
}

(async () => {
    // Text past element-size is cut with an ellipsis, and a larger size shows
    // more of it again; 0 shows all of it.
    {
        const bus = client();
        const row = new context.Item(bus, 5, 1, 1);

        await filled(row, bus, 'a', 'abcdefghij');
        assert.equal(row.label.clutter_text.text, 'abcde…');
        row.setTextSize(8);
        assert.equal(row.label.clutter_text.text, 'abcdefgh…');
        row.setTextSize(0);
        assert.equal(row.label.clutter_text.text, 'abcdefghij');
        row.setTextSize(20);
        assert.equal(row.label.clutter_text.text, 'abcdefghij');
        // Characters, not UTF-16 units: no surrogate pair is cut in two.
        row.setTextSize(2);
        row.refresh();
        await filled(row, bus, 'a', '😀😀😀');
        assert.equal(row.label.clutter_text.text, '😀😀…');
    }

    // Rebound to another uuid, the row selects and deletes nothing until that
    // item is what it shows; then it acts on it, saying so in the log if the
    // daemon refuses.
    {
        const bus = client();
        const row = new context.Item(bus, 50, 1, -1, 'first');

        await filled(row, bus, 'first', 'first');
        row.setUuid('next');
        row.activate();
        row._delete();
        assert.deepEqual(bus.sent, []);
        assert.equal(row.activated, undefined);

        await filled(row, bus, 'next', 'next');
        row.activate();
        row._delete();
        assert.deepEqual(bus.sent.map(({method, uuid, callback}) => [method, uuid, callback.finish]),
            [['select', 'next', 'select_finish'], ['delete_item', 'next', 'delete_item_finish']]);
        assert.equal(row.activated, true);
    }

    // Rebound to a position, as the history list rebinds its rows, it acts on
    // nothing until the item there is what it shows; a reply a newer binding
    // overtook paints nothing; and destroyed, the read it has out is
    // cancelled.
    {
        const bus = client();
        const row = new context.Item(bus, 50, 1, -1, 'first');

        await filled(row, bus, 'first', 'first');
        row.setIndex(2);
        row.activate();
        row._delete();
        assert.deepEqual(bus.sent, []);

        row.setUuid('older');

        const stale = bus.pending;

        row.setUuid('newer');
        stale.resolve(item('older', 'stale'));
        await new Promise(resolve => setImmediate(resolve));
        assert.notEqual(row.label.clutter_text.text, 'stale');

        row.destroy();
        assert.equal(bus.cancellable.is_cancelled(), true);
    }

    // The menu key on the row's star or bin, which St emits popup-menu on,
    // opens the row's actions as it does on the row.
    {
        const bus = client();
        const row = new context.Item(bus, 50, 1, 1);
        const asked = [];

        row.connect('actions', (actor, uuid, fromKeyboard) => asked.push([uuid, fromKeyboard]));
        await filled(row, bus, 'text', 'text');
        row._favouriteItem.emit('popup-menu');
        row._deleteItem.emit('popup-menu');
        assert.deepEqual(asked, [['text', true], ['text', true]]);
    }

    // A read failing for a row is said at debug level, not as an error.
    {
        const bus = client();
        const row = new context.Item(bus, 50, 1, 1);

        bus.pending.reject(new Error('INVALID_INDEX'));
        await new Promise(resolve => setImmediate(resolve));
        assert.deepEqual(errors, []);
        assert.deepEqual(debugged, ['INVALID_INDEX']);
        assert.ok(row);
    }

    // The star asks for the other state, either way, and logs a refusal, the
    // row's sibling file read the same way.
    {
        const sent = [];
        const starContext = vm.createContext({
            _: text => text,
            GObject: {registerClass: klass => klass},
            St: {
                Button: class { constructor() {} },
                Icon: class {},
            },
            logFailure: finish => ({finish}),
        });
        const starSource = fs.readFileSync(path.join(path.dirname(process.argv[2]), 'favouriteButton.js'), 'utf8')
            .replace(/^import [^;]*;\n/gm, '').replace('export const ', 'const ');

        vm.runInContext(`${starSource}\nthis.Star = GPasteFavouriteButton;`, starContext);

        const star = new starContext.Star({set_favourite: (uuid, favourite, cancellable, callback) => sent.push([uuid, favourite, callback.finish])}, 'pinned');

        star.vfunc_clicked();
        star.setFavourite(true);
        star.vfunc_clicked();
        assert.deepEqual(sent, [['pinned', true, 'set_favourite_finish'], ['pinned', false, 'set_favourite_finish']]);
    }

    console.log('Item: text bound, what the row acts on, the menu key and quiet failures passed');
})().catch(e => {
    console.log(e);
    process.exit(1);
});
