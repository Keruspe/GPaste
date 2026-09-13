// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

// Execute the shipped controller methods with deterministic actor/bus doubles.
// This checks lifecycle, draft ownership, the current row's count and what may
// set the switcher row's `active`, not Shell rendering or modal grabs.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

class Actor {
    static get_binding_pool() { return {install_closure() {}}; }
    vfunc_hide() { this.hidden = true; }
    get active() { return this._active ?? false; }
    set active(active) { this._active = active; }
}
class Modal extends Actor {
    constructor() {
        super();
        this.contentLayout = {add_child() {}};
        this.buttons = [];
    }
    addButton(button) { this.buttons.push(button); }
    connectObject(_signal, callback) { this.onDestroy = callback; }
    open() {}
    close() { this.closed = true; this.destroy(); }
    destroy() { this.destroyed = true; this.onDestroy?.(); }
}
let focus = null;
const errors = [];
const context = vm.createContext({
    console: {error: message => errors.push(message)},
    _: value => value,
    pgettext: (_context, value) => value,
    Dialog: {MessageDialogContent: class {}},
    ModalDialog: {ModalDialog: Modal},
    PopupBaseMenuItem: Actor,
    PopupMenuItem: Actor,
    PopupSubMenuMenuItem: Actor,
    GObject: {registerClass: (...args) => args.at(-1)},
    Clutter: {}, St: {}, Ornament: {}, GPasteDeleteButton: Actor,
    GPaste: {
        DEFAULT_HISTORY: 'default',
        UpdateAction: {REPLACE: 1, REMOVE: 2},
        UpdateTarget: {ALL: 1, ITEM: 2},
    },
    global: {stage: {get_key_focus: () => focus}},
    replaceCancellable: previous => {
        previous?.cancel();
        return {cancel() { this.cancelled = true; }};
    },
    // Asked once the reply is in, as the real one asks: a request a newer one
    // replaced can still land.
    awaitReply: async (cancellable, promise) => {
        const value = await promise;
        return [!cancellable.cancelled, value];
    },
});
const source = fs.readFileSync(process.argv[2], 'utf8')
    .replace(/^import .*;\n/gm, '').replace('export class ', 'class ');
vm.runInContext(`${source}\nthis.Switcher = GPasteHistorySwitcher;`, context);

function switcher() {
    const item = Object.create(context.Switcher.prototype);
    item._client = {get_name_owner: () => ':1.42'};
    item._current = null;
    item.menu = {isOpen: true, moveMenuItem() { focus = null; }, close() {}};
    item._getTopMenu = () => ({close() {}});
    item._rows = [];
    item._actions = {
        visible: true, history: 'work', draft: 'work_backup_edited',
        contains: actor => actor === item.entry,
        hide() { this.visible = false; },
        reset() { this.draft = ''; },
        revalidate() { this.revalidated = true; },
    };
    item.entry = {grab_key_focus() { focus = this; }};
    item.grab_key_focus = () => { focus = item; };
    item._clearRows = () => { item._rows = []; };
    // As the real one does, a listed default history sizes the row already drawn.
    item._addRow = (history, size) => {
        const known = item._rows.find(row => row.history === history);

        if (known) {
            known.setSize(size);
            return;
        }

        item._rows.push({
            history, size, contains: () => false,
            grab_key_focus() { focus = this; },
            setSize(newSize) { this.size = newSize; },
        });
    };
    item._markCurrent = () => {};
    return item;
}

(async () => {
    const item = switcher();
    item._client.list_histories = async () => [{get_name: () => 'work', get_size: () => 0}];
    focus = item.entry;
    await item.refresh();
    assert.equal(item._actions.draft, 'work_backup_edited');
    assert.equal(item._actions.visible, true);
    assert.equal(focus, item.entry);
    assert.equal(item._actions.revalidated, true);

    // Deletion of the source, unlike an unrelated count change, invalidates it.
    item._client.list_histories = async () => [];
    await item.refresh();
    assert.equal(item._actions.visible, false);
    assert.equal(item._actions.draft, '');
    assert.equal(focus, item);

    let calls = 0;
    item._confirm('Delete?', 'Contents', 'Delete', () => ++calls);
    const dialog = item._dialog;
    assert.ok(dialog);
    item._client.get_name_owner = () => null;
    item.vfunc_hide();
    assert.equal(dialog.closed, true);
    assert.equal(item._listing.cancelled, true);
    // A queued click can still reach the closure during dialog teardown.
    dialog.buttons[1].action();
    assert.equal(calls, 0);

    const available = switcher();
    available._confirm('Empty?', 'Contents', 'Empty', () => ++calls);
    available._dialog.buttons[1].action();
    assert.equal(calls, 1);


    // The pointer is not the keyboard: only what matches the key focus reaches
    // `active`, which the menu's own deactivation of a focused row cannot undo.
    const row = switcher();
    let hasFocus = false;
    row.has_key_focus = () => hasFocus;
    row.active = true;
    assert.equal(row.active, false);
    hasFocus = true;
    row.active = true;
    assert.equal(row.active, true);
    row.active = false;
    assert.equal(row.active, true);
    hasFocus = false;
    row.active = false;
    assert.equal(row.active, false);

    // A listing that does not name the history in use -- the `none` storage
    // lists nothing -- leaves its count to be asked for.
    const sized = switcher();
    let sizeCalls = 0;
    sized._current = 'default';
    sized._actions.visible = false;
    sized._client.get_history_size = async () => {
        ++sizeCalls;
        return 7;
    };
    sized._client.list_histories = async () => [];
    await sized.refresh();
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(sizeCalls, 1);
    assert.equal(sized._rows.find(r => r.history === 'default').size, 7);

    // A listing that does name it answers its count without another call.
    sized._client.list_histories = async () => [{get_name: () => 'default', get_size: () => 3}];
    await sized.refresh();
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(sizeCalls, 1);
    assert.equal(sized._rows.find(r => r.history === 'default').size, 3);

    // Only an update that can change the count asks for it, and only while the
    // chooser is open.
    const {UpdateAction, UpdateTarget} = context.GPaste;
    sized._onUpdate(null, UpdateAction.REPLACE, UpdateTarget.ITEM);
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(sizeCalls, 1);
    sized._onUpdate(null, UpdateAction.REMOVE, UpdateTarget.ITEM);
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(sizeCalls, 2);
    assert.equal(sized._rows.find(r => r.history === 'default').size, 7);
    sized._onUpdate(null, UpdateAction.REPLACE, UpdateTarget.ALL);
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(sizeCalls, 3);
    sized.menu.isOpen = false;
    sized._onUpdate(null, UpdateAction.REMOVE, UpdateTarget.ALL);
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(sizeCalls, 3);

    // An overtaken size is dropped rather than drawn over the newer one.
    sized.menu.isOpen = true;
    let resolveFirst;
    sized._client.get_history_size = () => {
        ++sizeCalls;
        return sizeCalls === 4 ? new Promise(resolve => { resolveFirst = resolve; }) : Promise.resolve(5);
    };
    const first = sized._sizeCurrent();
    await sized._sizeCurrent();
    resolveFirst(9);
    await first;
    assert.equal(sized._rows.find(r => r.history === 'default').size, 5);

    assert.deepEqual(errors, []);
    console.log('History switcher: draft refresh, source deletion, daemon loss, current count and row focus passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
