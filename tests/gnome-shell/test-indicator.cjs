// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

// Execute the shipped indicator's methods with deterministic actor/bus doubles.
// This checks its teardown and what the placeholder row says of the daemon,
// not Shell rendering or the menu's grab.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const errors = [];

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
    Button,
    Main: {layoutManager: {disconnectObject() {}}},
    GObject: {registerClass: (...args) => args.at(-1)},
    GLib: {Source: {remove() {}}},
    Clutter: {}, St: {BoxLayout: Actor, Icon: Actor},
    GPasteDummyHistoryItem: Actor,
    GPasteSearchItem: Actor,
    GPaste: {
        DaemonPresence: {ABSENT: 0, STARTING: 1, READY: 2},
        Settings: class {
            connectObject() {}
            disconnectObject() {}
            get_element_size() { return 50; }
        },
    },
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
// constructor: no client, no menu rows.
function indicator() {
    const item = Object.create(context.Indicator.prototype);
    item._settings = {disconnectObject() {}};
    item._history = [];
    item._listing = null;
    item._connectRetryId = 0;
    item._selectSearchId = 0;
    item._client = null;
    return item;
}

function row(uuid) {
    return {
        uuid,
        destroyed: false,
        destroy() { this.destroyed = true; },
    };
}

(async () => {
    // Teardown chains up to the panel button's, once, and before _setup () has
    // built the rows it reaches for.
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

    assert.deepEqual(errors, []);
    console.log('Indicator: teardown and daemon presence passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
