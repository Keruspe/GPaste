// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

// Execute the shipped indicator's methods with deterministic actor/bus doubles.
// This checks its teardown, what the placeholder row says of the daemon and
// where an item row's actions stay, not Shell rendering or the menu's grab.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

let focus = null;
const notified = [];
const copied = [];
const errors = [];
const weakRefs = [];

class WeakRefDouble {
    constructor(target) { this.target = target; weakRefs.push(this); }
    deref() { return this.target; }
}

// What PanelMenu.Button's own teardown does is destroy the menu: counted here,
// since running it twice or not at all are both what a subclass can get wrong.
// Its constructor connects that teardown to 'destroy', as the real one does, so
// an indicator built through its own constructor is torn down the way the
// shell tears it down.
class Button {
    constructor() {
        this.menu = {addMenuItem() {}};
        this._handlers = [];
        this.connect('destroy', () => this._onDestroy());
    }

    connect(signal, handler) { this._handlers.push({signal, handler}); }
    add_child() {}
    destroy() {
        for (const {signal, handler} of this._handlers) {
            if (signal === 'destroy')
                handler();
        }
    }

    _onDestroy() { this.buttonDestroyed = (this.buttonDestroyed ?? 0) + 1; }
    _onOpenStateChanged() {}
}

// What the constructor builds before _setup (), which the test stops short of.
class Actor {
    constructor() { this.children = []; }
    add_child(child) { this.children.push(child); }
    connect() {}
    resetSize() {}
}

const context = vm.createContext({
    console: {error: message => errors.push(message)},
    _: value => value,
    Button,
    Main: {
        layoutManager: {disconnectObject() {}},
        notify: (title, body) => notified.push({title, body}),
        notifyError: (title, body) => notified.push({title, body}),
    },
    GObject: {registerClass: (...args) => args.at(-1)},
    GLib: {Source: {remove() {}}},
    WeakRef: WeakRefDouble,
    Clutter: {},
    St: {
        BoxLayout: Actor,
        Icon: Actor,
        ClipboardType: {CLIPBOARD: 1},
        Clipboard: {get_default: () => ({set_text: (type, text) => copied.push({type, text})})},
    },
    GPasteDummyHistoryItem: Actor,
    GPasteSearchItem: Actor,
    GPaste: {
        DaemonPresence: {ABSENT: 0, STARTING: 1, READY: 2},
        UpdateAction: {REPLACE: 1, REMOVE: 2},
        UpdateTarget: {ALL: 1, ITEM: 2},
        Settings: class {
            connectObject() {}
            disconnectObject() {}
            get_element_size() { return 50; }
        },
    },
    global: {stage: {get_key_focus: () => focus}},
    ensureActorVisibleInScrollView() {},
    logFailure: () => () => {},
    replaceCancellable: previous => {
        previous?.cancel();
        return {cancel() { this.cancelled = true; }, is_cancelled() { return !!this.cancelled; }};
    },
});
const source = fs.readFileSync(process.argv[2], 'utf8')
    .replace(/^import [^;]*;\n/gm, '').replace('export const ', 'const ');
vm.runInContext(`${source}\nthis.Indicator = GPasteIndicator;`, context);

const {DaemonPresence} = context.GPaste;

// An indicator torn down before _setup () has built anything past the
// constructor: no client, no menu rows, no item actions.
function indicator() {
    const item = Object.create(context.Indicator.prototype);
    item._settings = {disconnectObject() {}};
    item._history = [];
    item._listing = null;
    item._connectRetryId = 0;
    item._selectSearchId = 0;
    item._client = null;
    item._owedRowFocus = -1;
    // What PanelMenu.Button's constructor builds, the menu's actor among it.
    item.menu = {actor: {grab_key_focus() { focus = this; }}};
    return item;
}

function row(uuid) {
    return {
        uuid,
        destroyed: false,
        destroy() { if (focus === this || focus === this.button) focus = null; this.destroyed = true; },
        grab_key_focus() { focus = this; },
        // Its pin and delete buttons, as one.
        button: {},
        contains(actor) { return actor === this.button; },
        setIndex: async () => {},
    };
}

// The item actions, as the one GPasteItemActionsItem the indicator moves.
function itemActions() {
    return {
        visible: false,
        _uuid: null,
        button: {},
        show() { this.visible = true; },
        hide() { this.visible = false; if (focus === this.button) focus = null; },
        reset(uuid = null) { this._uuid = uuid; },
        contains(actor) { return actor === this.button; },
        focus() { focus = this.button; },
    };
}

function withActions(uuids) {
    const item = indicator();
    item._history = uuids.map(row);
    item._itemActions = itemActions();
    item._itemActionsRow = null;
    item._scrollView = {};
    item._historySection = {moveMenuItem(actor, position) { item.movedTo = position; }};
    item._historySwitcher = {grab_key_focus() { focus = this; }};
    item._client = {daemon_presence: DaemonPresence.READY};
    item._fetchAvailable = async () => true;
    item._updateVisibility = () => {};
    item._maybeLoadMore = () => {};
    return item;
}

(async () => {
    // Teardown chains up to the panel button's, once, and before _setup () has
    // built the rows and the item actions it reaches for.
    const early = indicator();
    early._onDestroy();
    assert.equal(early._destroyed, true);
    assert.equal(early.buttonDestroyed, 1);

    // Built through its own constructor, and destroyed as the shell does it: the
    // button's connection is the only one running the teardown, so it runs
    // once.
    class Constructed extends context.Indicator {
        _setup() { return Promise.resolve(); }
    }
    const built = new Constructed({});
    built.destroy();
    assert.equal(built._destroyed, true);
    assert.equal(built.buttonDestroyed, 1);

    // What the placeholder row says while there is no daemon is the client's
    // presence: starting reads "Loading…", absent offers the retry.
    const placeholder = indicator();
    const shown = [];
    placeholder._connected = false;
    placeholder._connecting = false;
    placeholder._dummyHistoryItem = {
        showLoading: () => shown.push('loading'),
        showDisconnected: () => shown.push('disconnected'),
    };
    placeholder._searchItem = {hide() {}, show() {}};
    placeholder._client = {daemon_presence: DaemonPresence.STARTING};
    placeholder._updateVisibility(true);
    placeholder._client.daemon_presence = DaemonPresence.ABSENT;
    placeholder._updateVisibility(true);
    // A proxy still being built is starting too, with nothing to retry yet.
    placeholder._connecting = true;
    placeholder._updateVisibility(true);
    placeholder._connecting = false;
    assert.deepEqual(shown, ['loading', 'disconnected', 'loading']);

    // Starting turning into absent is no edge, and still repaints the row.
    shown.length = 0;
    placeholder._onDaemonPresenceChanged();
    assert.deepEqual(shown, ['disconnected']);

    // A daemon appearing reloads; one going away forgets the rows.
    let reloads = 0;
    placeholder._reloadCurrent = () => ++reloads;
    placeholder._client.report_extension_state = () => {};
    placeholder._client.daemon_presence = DaemonPresence.READY;
    placeholder._onDaemonPresenceChanged();
    assert.equal(placeholder._connected, true);
    assert.equal(reloads, 1);
    const kept = row('kept');
    placeholder._history = [kept];
    placeholder._client.daemon_presence = DaemonPresence.ABSENT;
    placeholder._onDaemonPresenceChanged();
    assert.equal(placeholder._connected, false);
    assert.equal(kept.destroyed, true);
    // The rows array is the vm's, so compared by length.
    assert.equal(placeholder._history.length, 0);

    // The row's retry is the client's, which starts its wait over.
    let retries = 0;
    placeholder._destroyed = false;
    placeholder._client.retry_daemon = () => ++retries;
    placeholder._retry();
    assert.equal(retries, 1);

    // And a teardown stops that wait: the switcher holds the client too.
    // Stopping moves the presence, whose notify must find our handlers gone.
    let unfollowed = 0;
    let connected = true;
    let lateNotifies = 0;
    placeholder._client.unfollow_daemon = () => {
        ++unfollowed;
        if (connected)
            ++lateNotifies;
    };
    placeholder._client.disconnectObject = () => { connected = false; };
    placeholder._onDestroy();
    assert.equal(unfollowed, 1);
    assert.equal(lateNotifies, 0);
    assert.equal(placeholder._client, null);

    // While the daemon is away, an update is not applied, nor is a size read:
    // mid-handoff the old daemon, standing down, is the one it reaches, and
    // the history the proxy has cached is its too.
    const handoff = indicator();
    let reads = 0;
    handoff._connected = false;
    handoff._refresh = async () => { ++reads; };
    handoff._client = {
        daemon_presence: DaemonPresence.STARTING,
        get_history_name: () => 'history',
        get_history_size: () => { ++reads; return Promise.resolve(1); },
    };
    handoff._update(handoff._client, 0, 0, '', 0);
    assert.equal(reads, 0);
    assert.equal(await context.Indicator.prototype._fetchAvailable.call(handoff, null), false);
    assert.equal(reads, 0);

    // The actions open under the row that asked, and the same row folds them.
    const actions = withActions(['a', 'b', 'c', 'd']);
    const [, second, third] = actions._history;
    actions._toggleItemActions(third, 'c', true);
    assert.equal(actions._itemActions.visible, true);
    assert.equal(actions._itemActions._uuid, 'c');
    assert.equal(actions.movedTo, 3);
    assert.equal(focus, actions._itemActions.button);
    actions._toggleItemActions(third, 'c', true);
    assert.equal(actions._itemActions.visible, false);
    // Folded with the focus in them, which goes back to the row.
    assert.equal(focus, third);

    // A refresh rebinding only the rows after theirs leaves them be...
    actions._toggleItemActions(second, 'b', false);
    actions._available = 4;
    await actions._refresh(2);
    assert.equal(actions._itemActions.visible, true);
    assert.equal(actions._itemActions._uuid, 'b');

    // ...one rebinding their row folds them, that row now showing another item...
    await actions._refresh(1);
    assert.equal(actions._itemActions.visible, false);
    assert.equal(actions._itemActionsRow, null);

    // ...and so does one dropping it, the history having shrunk under it.
    const [last] = actions._history.slice(-1);
    actions._toggleItemActions(last, 'd', false);
    actions._available = 3;
    await actions._refresh(3);
    assert.equal(actions._itemActions.visible, false);
    assert.equal(last.destroyed, true);

    // A keyboard-opened action row hands focus to a surviving row after the
    // last row is destroyed.
    const shrinking = withActions(['a', 'b']);
    const [survivor, removed] = shrinking._history;
    shrinking._toggleItemActions(removed, 'b', true);
    shrinking._available = 1;
    await shrinking._refresh(1);
    assert.equal(focus, survivor);

    // So does a focused row of its own, or one of its buttons, that a refresh
    // drops: Delete on the last row, say.
    for (const target of ['row', 'button']) {
        const deleting = withActions(['a', 'b']);
        const [kept, deleted] = deleting._history;
        focus = target === 'row' ? deleted : deleted.button;
        deleting._available = 1;
        await deleting._refresh(1);
        assert.equal(focus, kept);
    }

    // When a filter or full reload removes every row, the history switcher is
    // the menu's remaining keyboard target.
    const emptied = withActions(['a']);
    emptied._toggleItemActions(emptied._history[0], 'a', true);
    emptied._available = 0;
    emptied._settings = {get_element_size: () => 50};
    emptied._isFiltered = () => false;
    emptied._totalSize = () => 0;
    emptied._fillBatch = () => 1;
    emptied._scrollToTop = () => {};
    emptied._rebuild(true);
    assert.equal(focus, emptied._historySwitcher);

    // So is it when a fetch gives up on a daemon still there, the rows going
    // without a change of presence -- the keyboard in the actions under one of
    // them included.
    const unanswered = withActions(['a', 'b']);
    unanswered._connected = true;
    unanswered._toggleItemActions(unanswered._history[1], 'b', true);
    unanswered._reconcileConnection({is_cancelled: () => false});
    assert.equal(unanswered._history.length, 0);
    assert.equal(focus, unanswered._historySwitcher);

    // The daemon going, a handoff to another included, takes the rows and
    // everything else in the menu that could hold the focus: the menu holds
    // it meanwhile, and the row it was on gets it back once the next daemon's
    // rows are there.
    const away = withActions(['a', 'b']);
    away._connected = true;
    focus = away._history[1].button;
    away._client.daemon_presence = DaemonPresence.STARTING;
    away._onDaemonPresenceChanged();
    assert.equal(away._connected, false);
    assert.equal(away._history.length, 0);
    assert.equal(focus, away.menu.actor);
    assert.equal(away._owedRowFocus, 1);
    // Gone twice, the second finding the focus where the first left it, which
    // is not a row to be owed.
    away._onDaemonGone();
    assert.equal(away._owedRowFocus, 1);
    // The placeholder turning sensitive after the grace second has the Shell
    // hand it the menu's focus, which is still the row's to be given back.
    away._dummyHistoryItem = {};
    focus = away._dummyHistoryItem;
    assert.equal(away._focusedRowIndex(), 1);
    away._client.daemon_presence = DaemonPresence.READY;
    away._client.report_extension_state = () => {};
    away._reloadCurrent = () => {};
    away._onDaemonPresenceChanged();
    away._settings = {get_element_size: () => 50};
    away._isFiltered = () => false;
    away._totalSize = () => 3;
    away._fillBatch = () => 3;
    away._scrollToTop = () => {};
    away._createRow = (size, slot) => away._history.push(row(String(slot)));
    away._rebuild(false);
    assert.equal(focus, away._history[1]);
    assert.equal(away._owedRowFocus, -1);

    // The menu closing while the focus is parked takes the debt with it: the
    // next opening is not given a row the user has long left.
    const closing = withActions(['a', 'b']);
    closing._owedRowFocus = 1;
    focus = closing.menu.actor;
    closing._updateIndexVisibility = () => {};
    closing._historySwitcher.collapse = () => {};
    closing._onOpenStateChanged(closing.menu, false);
    assert.equal(closing._owedRowFocus, -1);

    // A failed upload reaches the user, not only the log.
    const upload = withActions(['a']);
    upload._client.upload_and_copy = (uuid, cancellable, callback) => callback(upload._client, null);
    upload._client.upload_and_copy_finish = () => { throw new Error('no network'); };
    upload._upload('a');
    assert.equal(notified.length, 1);
    assert.equal(notified[0].title, 'Could not upload the item');
    assert.equal(notified[0].body, 'no network');
    assert.equal(errors.pop().message, 'no network');

    // The call outlives an indicator the runtime has collected. Its callback
    // still finishes the D-Bus reply without reaching for that indicator.
    notified.length = 0;
    let lateUpload;
    const collected = withActions(['a']);
    collected._client.upload_and_copy = (uuid, cancellable, callback) => { lateUpload = callback; };
    collected._client.upload_and_copy_finish = () => 'https://paste.rs/late';
    collected._upload('a');
    assert.equal(weakRefs.length > 0, true);
    weakRefs.at(-1).target = undefined;
    lateUpload(collected._client, null);
    assert.equal(notified.length, 0);

    // A done one is said too, without the address, which the daemon has kept
    // (onUploadDone ()): the menu writes no clipboard of its own
    // (UploadAndCopy).
    notified.length = 0;
    const uploaded = withActions(['a']);
    uploaded._client.upload_and_copy = (uuid, cancellable, callback) => callback(uploaded._client, null);
    uploaded._client.upload_and_copy_finish = () => 'https://paste.rs/x';
    uploaded._upload('a');
    assert.deepEqual(copied, []);
    assert.equal(notified.length, 1);
    assert.equal(notified[0].body, undefined);

    // Nothing reaches the screen from an indicator torn down meanwhile, but a
    // failure still reaches the log.
    notified.length = 0;
    uploaded._destroyed = true;
    uploaded._upload('a');
    uploaded._client.upload_and_copy_finish = () => { throw new Error('no network'); };
    uploaded._upload('a');
    assert.equal(notified.length, 0);
    assert.equal(errors.pop().message, 'no network');

    assert.deepEqual(errors, []);
    console.log('Indicator: teardown, daemon presence and item actions passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
