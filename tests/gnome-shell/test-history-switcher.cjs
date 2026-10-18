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
        Client: {is_daemon_gone_error: e => !!e.gone},
        DaemonPresence: {ABSENT: 0, STARTING: 1, READY: 2},
        UpdateAction: {REPLACE: 1, REMOVE: 2},
        UpdateTarget: {ALL: 1, ITEM: 2},
    },
    global: {stage: {get_key_focus: () => focus}},
    // As the real one does: the call finished, a refusal logged.
    logFailure: finish => (client, result) => {
        try {
            client[finish](result);
        } catch (e) {
            errors.push(e.message);
        }
    },
    daemonGone: e => !!e.gone,
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
    item._client = {daemon_presence: 2};
    item._current = null;
    item.menu = {isOpen: true, moveMenuItem() { focus = null; }, addMenuItem() {}, close() {}};
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
    item._newRow = (history, size) => ({
        history, size,
        contains(actor) { return actor === this; },
        grab_key_focus() { focus = this; },
        setSize(newSize) { this.size = newSize; },
        destroy() {
            this.destroyed = true;
            if (focus === this)
                focus = null;
        },
    });
    item._markCurrent = () => {};
    return item;
}

// As runSuite () in suite.cjs does.
let finished = false;
process.on('exit', () => {
    if (!finished) {
        console.error('History switcher: the suite stopped on a promise that never settled');
        process.exitCode = 1;
    }
});

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

    // A listing keeps the row of every history it still names, in its order,
    // the keyboard staying on it; the row of one gone is destroyed, its focus
    // going to the switcher row.
    const kept = switcher();
    const listing = (sizes, names) => async () => names.map(name => ({get_name: () => name, get_size: () => sizes}));
    // The menu's items, placed as PopupMenuBase places them: a position counts
    // the items other than the one placed.
    const placed = [];
    const shownRows = () => placed.filter(item => item !== kept._actions);

    kept.menu.addMenuItem = (item, position) => placed.splice(position, 0, item);
    kept.menu.moveMenuItem = (item, position) => {
        if (placed.includes(item))
            placed.splice(placed.indexOf(item), 1);
        placed.splice(position, 0, item);
        focus = null;
    };
    kept._actions.visible = false;
    kept._client.list_histories = listing(1, ['work', 'play']);
    await kept.refresh();

    const [defaultRow, workRow, playRow] = kept._rows;

    assert.deepEqual([...kept._rows].map(r => r.history), ['default', 'work', 'play']);
    workRow.grab_key_focus();
    kept._client.list_histories = listing(2, ['play', 'work']);
    await kept.refresh();
    assert.deepEqual([...kept._rows], [defaultRow, playRow, workRow]);
    assert.deepEqual(shownRows(), [defaultRow, playRow, workRow]);
    assert.equal(workRow.size, 2);
    assert.equal(focus, workRow);
    kept._client.list_histories = listing(2, ['play']);
    await kept.refresh();
    assert.deepEqual([...kept._rows], [defaultRow, playRow]);
    assert.equal(workRow.destroyed, true);
    placed.splice(placed.indexOf(workRow), 1);
    assert.deepEqual(shownRows(), [defaultRow, playRow]);
    assert.equal(playRow.destroyed, undefined);
    assert.equal(focus, kept);

    let calls = 0;
    item._confirm('Delete?', 'Contents', 'Delete', () => ++calls);
    const dialog = item._dialog;
    assert.ok(dialog);
    // Mid-handoff: the name still has an owner, the daemon standing down.
    item._client.get_name_owner = () => ':1.42';
    item._client.daemon_presence = 1;
    item._sizing = {cancel() { this.cancelled = true; }};
    item.vfunc_hide();
    assert.equal(dialog.closed, true);
    // The daemon going -- a handoff to another included, the presence leaving
    // ready for it -- hides the row, and that is all that gives up the answers
    // still due from the old daemon: a late size would land on the new one's
    // current row.
    assert.equal(item._listing.cancelled, true);
    assert.equal(item._sizing.cancelled, true);
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

    // A listing that does not name the history in use -- a GPaste 51.0 to
    // 51.2 daemon's under the `none` storage -- leaves its count to be asked
    // for.
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

    // A history in use that the listing does not name is drawn all the same,
    // its count asked for, and the actions open for it stay.
    const unlisted = switcher();
    let unlistedSizes = 0;

    unlisted._current = 'work';
    unlisted._client.get_history_size = async () => {
        ++unlistedSizes;
        return 4;
    };
    unlisted._client.list_histories = async () => [];
    await unlisted.refresh();
    await new Promise(resolve => setImmediate(resolve));
    assert.deepEqual([...unlisted._rows].map(r => r.history), ['default', 'work']);
    assert.equal(unlisted._rows[1].size, 4);
    assert.equal(unlistedSizes, 1);
    assert.equal(unlisted._actions.visible, true);
    // Switched to another such history, the chooser lists again to draw it;
    // switched to one it has a row for, it does not.
    let relisted = 0;

    unlisted.label = {};
    unlisted.refresh = async () => { ++relisted; };
    unlisted._client.get_history_name = () => 'other';
    unlisted._onHistoryChanged();
    assert.equal(relisted, 1);
    unlisted._client.get_history_name = () => 'work';
    unlisted._onHistoryChanged();
    assert.equal(relisted, 1);

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
    // Nor while another daemon takes the name over, the old one, which still
    // owns it, being the one the call would reach.
    sized.menu.isOpen = true;
    sized._client.get_name_owner = () => ':1.42';
    sized._client.daemon_presence = 1;
    sized._onUpdate(null, UpdateAction.REMOVE, UpdateTarget.ALL);
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(sizeCalls, 3);
    // The chooser's listing, asked on opening the submenu, waits for a daemon
    // to ask as well.
    let refreshes = 0;
    sized.refresh = async () => { ++refreshes; };
    sized._client.daemon_presence = 1;
    sized._refreshIfOpen();
    assert.equal(refreshes, 0);
    sized._client.daemon_presence = 2;
    sized._refreshIfOpen();
    assert.equal(refreshes, 1);
    delete sized.refresh;

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

    // A listing or a size failing because the daemon left is not logged, the
    // presence saying so next; any other failure is.
    const leaving = switcher();

    for (const gone of [true, false]) {
        const failure = Object.assign(new Error('NoReply'), {gone});

        leaving._client.list_histories = () => Promise.reject(failure);
        leaving._client.get_history_size = () => Promise.reject(failure);
        await leaving.refresh();
        await leaving._sizeCurrent();
        assert.deepEqual(errors.splice(0).map(e => e.message), gone ? [] : ['NoReply', 'NoReply']);
    }

    // A backup nothing waits on reports a refusal to the log.
    const backup = switcher();
    backup._actions.hide = () => {};
    backup._client.backup_history = (history, name, cancellable, callback) => callback(backup._client, null);
    backup._client.backup_history_finish = () => { throw new Error('already exists'); };
    backup._backup('work', 'work_backup');
    assert.deepEqual(errors.splice(0), ['already exists']);

    assert.deepEqual(errors, []);
    console.log('History switcher: draft refresh, source deletion, daemon loss, current count, row focus and call reporting passed');
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(() => { finished = true; });
