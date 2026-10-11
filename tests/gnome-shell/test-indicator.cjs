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
const timers = [];
const removedSources = [];
const shownActors = [];
const themeContext = {scaleFactor: 1, disconnectObject() {}};

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
    constructor() { this.children = []; this.handlers = {}; }
    add_child(child) { this.children.push(child); }
    connect(signal, handler) { this.handlers[signal] = handler; }
    resetSize() {}
}

const context = vm.createContext({
    console: {error: message => errors.push(message)},
    _: value => value,
    Button,
    Main: {
        layoutManager: {
            disconnectObject() {},
            findIndexForActor: () => 0,
            getWorkAreaForMonitor: () => ({height: 1000}),
        },
        notify: (title, body) => notified.push({title, body}),
        notifyError: (title, body) => notified.push({title, body}),
    },
    GObject: {registerClass: (...args) => args.at(-1)},
    GLib: {
        Source: {remove: id => removedSources.push(id), set_name_by_id() {}},
        Regex: {escape_string: text => `escaped(${text})`},
        PRIORITY_DEFAULT: 0,
        timeout_add_once: (priority, delay, callback) => timers.push({delay, callback}),
    },
    WeakRef: WeakRefDouble,
    Clutter: {},
    St: {
        BoxLayout: Actor,
        Icon: Actor,
        ClipboardType: {CLIPBOARD: 1},
        ThemeContext: {get_for_stage: () => themeContext},
        Clipboard: {get_default: () => ({set_text: (type, text) => copied.push({type, text})})},
    },
    GPasteDummyHistoryItem: Actor,
    GPasteSearchItem: Actor,
    GPaste: {
        Error: {INVALID_ARGUMENT: 9},
        Client: {is_daemon_gone_error: e => !!e.gone},
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
    ensureActorVisibleInScrollView: (view, actor) => shownActors.push(actor),
    logFailure: finish => Object.assign(() => {}, {finish}),
    awaitReply: async (cancellable, call) => {
        const reply = await call;
        return [!cancellable.is_cancelled(), reply];
    },
    debugFailure() {},
    daemonGone: e => !!e.gone,
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
    item._filteredUuids = [];
    item._listing = null;
    item._connectRetryId = 0;
    item._selectSearchId = 0;
    item._client = null;
    item._owedRowFocus = -1;
    item._owedSearchFocus = false;
    item._passFrom = null;
    item._passKeepsPlace = true;
    // What PanelMenu.Button's constructor builds, the menu's actor among it.
    item.menu = {actor: {grab_key_focus() { focus = this; }, contains: () => false}};
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
    const shared = new context.GPaste.Settings();
    const built = new Constructed({settings: shared});

    // It reads the extension's settings rather than building its own, the one
    // instance the process has.
    assert.equal(built._settings, shared);

    // A keystroke in its search waits for the typing to pause.
    const waiting = timers.length;

    built._reloadCurrent = () => assert.fail('searched at once');
    built._hasSearch = () => true;
    built._searchItem.handlers['text-changed']();
    assert.equal(timers.length, waiting + 1);
    removedSources.length = 0;
    built.destroy();
    assert.ok(removedSources.includes(waiting + 1));
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
    const reports = [];
    placeholder._client.report_extension_state = (state, cancellable, callback) => reports.push([state, callback.finish]);
    placeholder._client.daemon_presence = DaemonPresence.READY;
    placeholder._onDaemonPresenceChanged();
    assert.equal(placeholder._connected, true);
    assert.equal(reloads, 1);
    // Reported there, a refusal logged rather than dropped.
    assert.deepEqual(reports, [[true, 'report_extension_state_finish']]);
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

    // A refresh finding items where the list has no row makes them, the
    // viewport of an empty list laid out at no height; one with rows leaves
    // the growing to _maybeLoadMore ().
    for (const [uuids, loads] of [[[], 1], [['a'], 0]]) {
        const growing = withActions(uuids);
        let loaded = 0;
        growing._loadMore = () => { ++loaded; };
        growing._available = 3;
        await growing._refresh(0);
        assert.equal(loaded, loads);
    }

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

    // A fetch giving up on a daemon still there -- one that failed, or whose
    // call failed as it was leaving -- leaves the rows it could not refresh as
    // they were, and the keyboard where it was...
    const unanswered = withActions(['a', 'b']);
    unanswered._connected = true;
    unanswered._toggleItemActions(unanswered._history[1], 'b', true);
    unanswered._reconcileConnection({is_cancelled: () => false});
    assert.equal(unanswered._history.length, 2);
    assert.equal(focus, unanswered._itemActions.button);

    // ...and with no rows to leave, the placeholder offers to ask again, which
    // a daemon still there answers with a reload -- the controls a daemon there
    // offers shown with it, though the pass failing is the first since it
    // turned up, which nothing else would have shown them for.
    const unlisted = withActions([]);
    let reasked = 0;

    unlisted._connected = true;
    unlisted._dummyHistoryItem = {
        hide() { this.state = 'hidden'; },
        showFailed() { this.state = 'failed'; },
        showLoading() { this.state = 'loading'; },
    };
    unlisted._switch = {visible: false};
    unlisted._historySwitcher.visible = false;
    unlisted._searchItem = {favourites: false, show() { this.shown = true; }, hide() { this.shown = false; }};
    unlisted._hasSearch = () => false;
    delete unlisted._updateVisibility;
    unlisted._reloadCurrent = () => ++reasked;
    unlisted._reconcileConnection({is_cancelled: () => false});
    assert.equal(unlisted._dummyHistoryItem.state, 'failed');
    assert.equal(unlisted._switch.visible, true);
    assert.equal(unlisted._historySwitcher.visible, true);
    assert.equal(unlisted._searchItem.shown, true);
    unlisted._retry();
    assert.equal(unlisted._dummyHistoryItem.state, 'loading');
    assert.equal(reasked, 1);

    // A reload whose fetch fails leaves no row bound to the filtered answer it
    // was to replace -- the search just cleared, a list counted unfiltered --
    // and the next pass its first row but not its place.
    const cleared = withActions(['x', 'y']);
    let clearedReconciled = 0;

    cleared._filteredUuids = ['x', 'y'];
    cleared._fetchAvailable = async () => false;
    cleared._reconcileConnection = () => ++clearedReconciled;
    await cleared._reload();
    assert.equal(cleared._history.length, 0);
    assert.equal(clearedReconciled, 1);
    assert.equal(cleared._passFrom, 0);
    assert.deepEqual([...cleared._beginPass(2, true)].slice(1), [0, true]);

    // A call failing because its daemon left logs nothing, the presence saying
    // so next; any other failure is logged.
    const leaving = withActions([]);
    let failure = Object.assign(new Error('gone'), {gone: true});

    leaving._client.get_history_size = () => Promise.reject(failure);
    leaving._client.search = () => Promise.reject(failure);
    leaving._searchItem = {text: 'x', favourites: false};
    leaving._hasSearch = () => true;
    leaving._reconcileConnection = () => {};
    leaving._daemonReady = () => true;
    delete leaving._fetchAvailable;
    assert.equal(await leaving._fetchAvailable({is_cancelled: () => false}), false);
    await leaving._runFilter();
    assert.deepEqual(errors, []);
    failure = new Error('broken');
    assert.equal(await leaving._fetchAvailable({is_cancelled: () => false}), false);
    assert.equal(errors.pop().message, 'broken');
    await leaving._runFilter();
    assert.equal(errors.pop().message, 'broken');

    // A pass giving up the one before rebinds the rows that one was to: a
    // refresh from the fourth row superseding one from the second rebinds from
    // the second, one superseding a reload every row, and one alone its own.
    const passes = withActions(['a', 'b', 'c', 'd']);
    const answers = [];
    let bound = [];

    for (const passRow of passes._history)
        passRow.setIndex = async index => { bound.push(index); };
    passes._available = 4;
    passes._fetchAvailable = () => new Promise(resolve => answers.push(resolve));
    passes._rebuild = () => bound.push('rebuilt');
    for (const [first, rebinds] of [[() => passes._refresh(1), [1, 2, 3]], [() => passes._reload(), [0, 1, 2, 3]]]) {
        bound = [];
        const superseded = first();
        const superseding = passes._refresh(3);

        answers.shift()(false);
        answers.shift()(true);
        await Promise.all([superseded, superseding]);
        assert.deepEqual(bound, rebinds);
        assert.equal(passes._passFrom, null);
    }
    bound = [];
    const alone = passes._refresh(2);

    answers.shift()(true);
    await alone;
    assert.deepEqual(bound, [2, 3]);

    // And it goes back to the top if the one it gives up was to: an update's
    // re-run superseding a new search, or a refresh an emptied one. Two
    // re-runs, or a refresh alone, leave the list where it was.
    {
        const placed = withActions(['a', 'b']);
        const searches = [];
        const kept = [];
        let scrolledTop = 0;

        placed._searchItem = {text: 'x', favourites: false};
        placed._hasSearch = () => true;
        placed._client.search = () => new Promise(resolve => searches.push(resolve));
        placed._rebuild = (empty, keepPlace) => kept.push(keepPlace);
        for (const [first, second, keepsPlace] of [[false, true, false], [true, true, true]]) {
            const superseded = placed._runFilter(first);
            const superseding = placed._runFilter(second);

            searches.shift()([]);
            searches.shift()([]);
            await Promise.all([superseded, superseding]);
            assert.deepEqual(kept.splice(0), [keepsPlace]);
        }
        placed._available = 2;
        placed._fetchAvailable = () => new Promise(resolve => answers.push(resolve));
        placed._scrollToTop = () => ++scrolledTop;
        for (const [first, scrolls] of [[() => placed._reload(), 1], [null, 0]]) {
            scrolledTop = 0;
            const superseded = first?.();
            const superseding = placed._refresh(1);

            if (superseded)
                answers.shift()(false);
            answers.shift()(true);
            await Promise.all([superseded, superseding]);
            assert.equal(scrolledTop, scrolls);
        }
    }

    // An update re-running the filter leaves the list where it was scrolled
    // to, the keyboard's row kept in view; a new search starts at the top.
    const relisted = withActions(['a', 'b', 'c']);
    let scrolledUp = 0;
    let keepPlace = null;

    for (const listedRow of relisted._history)
        listedRow.setUuid = async () => {};
    relisted._settings = {get_element_size: () => 50};
    relisted._isFiltered = () => true;
    relisted._filteredUuids = ['a', 'b', 'c'];
    relisted._totalSize = () => 3;
    relisted._fillBatch = () => 3;
    relisted._scrollToTop = () => ++scrolledUp;
    relisted._restoreSearchFocus = () => {};
    focus = relisted._history[2];
    shownActors.length = 0;
    relisted._rebuild(false, true);
    assert.equal(scrolledUp, 0);
    assert.deepEqual(shownActors, [relisted._history[2]]);
    relisted._rebuild(false);
    assert.equal(scrolledUp, 1);
    relisted._connected = true;
    relisted._searchItem = {favourites: false};
    relisted._runFilter = async place => { keepPlace = place; };
    relisted._update(null, context.GPaste.UpdateAction.REMOVE, context.GPaste.UpdateTarget.ITEM, 'b', 1);
    assert.equal(keepPlace, true);

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
    // An empty answer first -- the next daemon still loading its history --
    // has no row to give the focus to, and leaves it owed rather than spend it
    // on the history switcher.
    away._totalSize = () => 0;
    away._rebuild(true);
    assert.equal(focus, away._dummyHistoryItem);
    assert.equal(away._owedRowFocus, 1);
    // A refresh is what lists the rows then, and pays it once they are there.
    away._totalSize = () => 3;
    away._available = 3;
    away._loadMore = context.Indicator.prototype._loadMore;
    await away._refresh(0);
    assert.equal(focus, away._history[1]);
    assert.equal(away._owedRowFocus, -1);

    // A focus on the search, or anything else the daemon takes out of the
    // menu, is held the same way, and goes to the search once it is back --
    // or the history switcher, an empty history hiding the search.
    for (const searchVisible of [true, false]) {
        const searching = withActions(['a']);
        const entry = {};

        searching._connected = true;
        searching.menu.actor.contains = actor => actor === entry;
        searching._updateVisibility = () => { if (focus === entry) focus = null; };
        focus = entry;
        searching._onDaemonGone();
        assert.equal(focus, searching.menu.actor);
        assert.equal(searching._owedSearchFocus, true);
        assert.equal(searching._owedRowFocus, -1);
        searching._connected = true;
        searching._searchItem = {visible: searchVisible, grabFocus() { focus = entry; }};
        searching._settings = {get_element_size: () => 50};
        searching._isFiltered = () => false;
        searching._totalSize = () => 1;
        searching._fillBatch = () => 1;
        searching._scrollToTop = () => {};
        searching._createRow = () => searching._history.push(row('a'));
        searching._rebuild(false);
        assert.equal(focus, searchVisible ? entry : searching._historySwitcher);
        assert.equal(searching._owedSearchFocus, false);
    }

    // The menu closing while the focus is parked takes the debt with it: the
    // next opening is not given a row the user has long left. And a search
    // still waiting for the typing to pause goes too, never run on the closed
    // menu.
    const closing = withActions(['a', 'b']);
    closing._owedRowFocus = 1;
    closing._owedSearchFocus = true;
    focus = closing.menu.actor;
    closing._updateIndexVisibility = () => {};
    closing._historySwitcher.collapse = () => {};
    closing._hasSearch = () => true;
    closing._onSearchChanged();
    removedSources.length = 0;
    closing._onOpenStateChanged(closing.menu, false);
    assert.equal(closing._owedRowFocus, -1);
    assert.equal(closing._owedSearchFocus, false);
    assert.deepEqual(removedSources, [timers.length]);
    assert.equal(closing._searchDelayId, 0);

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

    // The search goes out as typed and as text: escaped, never lowercased. One
    // the daemon refuses as too long lists nothing, and is no failure.
    const filtered = indicator();
    const queries = [];
    let rebuilt = null;
    let reconciled = 0;

    filtered._client = {
        search: query => {
            queries.push(query);
            return Promise.reject(Object.assign(new Error('too long'), {
                matches: (domain, code) => domain === context.GPaste.Error && code === context.GPaste.Error.INVALID_ARGUMENT,
            }));
        },
    };
    filtered._searchItem = {text: 'MyBank (x', favourites: false};
    filtered._hasSearch = () => true;
    filtered._rebuild = empty => { rebuilt = empty; };
    filtered._reconcileConnection = () => ++reconciled;
    await filtered._runFilter();
    assert.deepEqual(queries, ['escaped(MyBank (x)']);
    assert.equal(rebuilt, true);
    assert.equal(reconciled, 0);

    // A keystroke's search waits for the typing to pause, GtkSearchEntry's
    // 150 ms; an emptied entry lists the history at once. A teardown and a
    // close stopping one still waiting are the built and closing cases'.
    const typed = indicator();
    let searches = 0;
    let searchText = 'a';

    typed._reloadCurrent = () => ++searches;
    typed._hasSearch = () => searchText.length > 0;
    removedSources.length = 0;
    typed._onSearchChanged();
    typed._onSearchChanged();
    assert.equal(searches, 0);
    assert.deepEqual(removedSources, [timers.length - 1]);
    assert.equal(timers.at(-1).delay, 150);
    timers.at(-1).callback();
    assert.equal(searches, 1);
    searchText = '';
    typed._onSearchChanged();
    assert.equal(searches, 2);

    // With element-size 0, no limit on the rows, the search box keeps the
    // width the default size gives it.
    const sized = indicator();
    const widths = [];

    sized._history = [];
    sized._searchItem = {resetSize: width => widths.push(width)};
    for (const size of [0, 80, 20]) {
        sized._settings = {get_element_size: () => size};
        sized._resetElementSize();
    }
    assert.deepEqual(widths, [43, 43, 13]);

    // The history's height is a share of the work area, in logical pixels as
    // the stylesheet is: the work area's physical ones divided by the scale.
    sized._scrollView = {};
    for (const [scaleFactor, height] of [[1, 600], [2, 300]]) {
        themeContext.scaleFactor = scaleFactor;
        sized._updateScrollHeight();
        assert.equal(sized._scrollView.style, `max-height: ${height}px`);
    }

    assert.deepEqual(errors, []);
    console.log('Indicator: teardown, daemon presence and item actions passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
