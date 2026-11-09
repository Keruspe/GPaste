// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

// Execute the shipped in-shell daemon runner with doubles for the bus, the
// daemon, the storage and the main loop: what standing down, a reconnect and a
// shutdown leave behind, how a runner waits on a store another is still
// rewriting, and how it runs a check of the store for the daemon. Not the
// daemon itself, which the C suites cover.
const assert = require('node:assert/strict');
const vm = require('node:vm');
const {loadShipped, runSuite, settle} = require('./suite.cjs');

const errors = [];
// Every main loop source, by id: what runs it is the test.
const sources = new Map();
let nextSource = 1;
const storage = {busy: false, unlocks: 0, passphrases: [], migrationChecks: 0, doctorError: null};
// What the daemon and the storage were asked for, in the order they were.
const happened = [];
const buses = [];
const daemons = [];
let failDaemon = false;
// An export failing, which the real bus reports from inside add_object () once
// its connection is up.
let failExport = false;

function addSource(kind, callback) {
    const id = nextSource++;

    sources.set(id, {kind, callback});
    return id;
}

// Runs every source of @kind once, dropping those that are done: an idle
// always, a timeout once it says so.
function runSources(kind) {
    for (const [id, source] of [...sources]) {
        if (source.kind !== kind)
            continue;

        if (kind === 'idle') {
            sources.delete(id);
            source.callback();
        } else if (!source.callback()) {
            sources.delete(id);
        }
    }
}

function countSources(kind) {
    return [...sources.values()].filter(source => source.kind === kind).length;
}

// Signals as connectObject () takes them: pairs, then the owner.
class Connected {
    connectObject(...args) {
        this.handlers = {};
        for (let i = 0; i + 1 < args.length; i += 2)
            this.handlers[args[i]] = args[i + 1];
    }

    disconnectObject() { this.handlers = null; }
}

class Bus extends Connected {
    constructor() {
        super();
        this.objects = [];
        this.connected = true;
        this.unowned = 0;
        buses.push(this);
    }

    own_name_full() {}
    unown_name() { ++this.unowned; }
    is_connected() { return this.connected; }
    add_object(object) {
        // As GJS refuses a null argument.
        if (object === null)
            throw new TypeError('Argument object may not be null');
        this.objects.push(object);
        if (failExport)
            this.emit('export-failed');
    }

    emit(signal, ...args) { this.handlers?.[signal]?.(this, ...args); }
}

class Daemon extends Connected {
    constructor() {
        super();
        this.disposed = false;
        this.flushes = 0;
        this.reloads = 0;
        this.doctored = null;
        daemons.push(this);
    }

    // Before the flush and the dispose, which leaves no clipboards manager to
    // take a password off the selection through.
    expire_password() { assert.equal(this.disposed, false); happened.push('expire'); }
    extension_state_changed(state) { (this.extensionStates ??= []).push(state); }
    flush() { assert.equal(this.disposed, false); ++this.flushes; happened.push('flush'); }
    reload_storage() { ++this.reloads; happened.push('reload'); }
    doctor_done(report, healthy) { this.doctored = [report, healthy]; }
    run_dispose() { this.disposed = true; }
}

const context = vm.createContext({
    console: {error: e => errors.push(e), log() {}},
    logError: e => errors.push(e),
    Promise,
    GLib: {
        PRIORITY_DEFAULT: 0,
        SOURCE_CONTINUE: true,
        SOURCE_REMOVE: false,
        // Only the once form, which is all an idle here has to be.
        idle_add_once: (priority, callback) => addSource('idle', callback),
        timeout_add: (priority, interval, callback) => addSource('timeout', callback),
        timeout_add_seconds: (priority, interval, callback) => addSource('timeout', callback),
        source_remove: id => sources.delete(id),
        Source: {
            remove: id => sources.delete(id),
            set_name_by_id: (id, name) => { sources.get(id).name = name; },
        },
    },
    GPasteDaemon: {
        Bus: {new: () => new Bus()},
        Daemon: {
            new_meta: () => {
                if (failDaemon)
                    return null;
                return new Daemon();
            },
        },
        SearchProvider: {new: () => ({})},
        StorageBackend: {
            unlock: () => ++storage.unlocks,
            set_passphrase: passphrase => storage.passphrases.push(passphrase),
        },
        storage_is_busy: () => storage.busy,
        // As the library's: ready whenever no worker runs, run once, under the
        // name it gives its source.
        storage_when_idle: callback => {
            const id = addSource('timeout', () => {
                if (storage.busy)
                    return true;
                callback();
                return false;
            });

            sources.get(id).name = '[GPaste] storage idle';
            return id;
        },
        storage_migration_needed: () => {
            ++storage.migrationChecks;
            return false;
        },
        storage_decryption_needed: () => false,
        storage_doctor_async: (prompt, settings, callback) => {
            happened.push('check');
            if (storage.holdDoctor)
                storage.heldDoctor = () => callback(null, {});
            else
                callback(null, {});
        },
        storage_doctor_finish: () => {
            if (storage.doctorError)
                throw new Error(storage.doctorError);
            return [true, ['Restricted the folder'], true];
        },
    },
    GPasteShellPrompt: class {
        constructor() { this.shutDowns = 0; }
        shutdown() { ++this.shutDowns; }
    },
    global: {display: {get_selection: () => ({})}},
});
loadShipped(context, {Runner: 'GPasteDaemonRunner'});

const settings = {reload() {}};

// A runner whose bus has acquired the name and built the daemon.
async function acquired() {
    const runner = new context.Runner(settings);
    const bus = buses.at(-1);

    bus.emit('name-acquired');
    await runner.ready;
    return [runner, bus];
}

runSuite('Daemon runner', async () => {
    // Another daemon taking the name over: the one built here is flushed and
    // disposed rather than left to the collector, the bus let go of, the
    // prompt shut down, and the passphrase wiped with the lock.
    let [runner, bus] = await acquired();
    let [daemon] = daemons.slice(-1);

    assert.equal(bus.objects.length, 2);
    happened.length = 0;
    bus.emit('name-lost', true);
    // A password counting down taken off the clipboard first, while the
    // daemon can still reach it.
    assert.deepEqual(happened, ['expire', 'flush']);
    assert.equal(daemon.flushes, 1);
    assert.equal(daemon.disposed, true);
    assert.equal(bus.unowned, 1);
    assert.equal(bus.handlers, null);
    assert.equal(runner._bus, null);
    assert.equal(runner._prompt.shutDowns, 1);
    assert.equal(storage.unlocks, 1);
    assert.deepEqual(storage.passphrases, [null]);
    // A disable reaching it after: nothing more to end, the lock's release
    // being one with nothing to release.
    runner.shutdown();
    assert.equal(daemon.flushes, 1);
    assert.equal(runner._prompt.shutDowns, 2);

    // The connection dropping is no takeover: the daemon goes all the same,
    // but the prompt and the passphrase stay for the reconnect.
    storage.passphrases.length = 0;
    [runner, bus] = await acquired();
    [daemon] = daemons.slice(-1);
    bus.connected = false;
    bus.emit('name-lost', true);
    assert.equal(daemon.disposed, true);
    assert.equal(runner._prompt.shutDowns, 0);
    assert.deepEqual(storage.passphrases, []);
    runner.shutdown();
    assert.deepEqual(storage.passphrases, [null]);

    // Shut down while a concern it let go of still rewrites the store: the
    // lock and the passphrase wait for it, the concern setting the
    // passphrase it settled on as it ends.
    storage.passphrases.length = 0;
    storage.unlocks = 0;
    [runner, bus] = await acquired();
    storage.busy = true;
    runner.shutdown();
    assert.equal(storage.unlocks, 0);
    assert.deepEqual(storage.passphrases, []);
    runSources('timeout');
    assert.equal(storage.unlocks, 0);
    storage.busy = false;
    storage.passphrases.push('settled');
    runSources('timeout');
    assert.equal(storage.unlocks, 1);
    assert.deepEqual(storage.passphrases, ['settled', null]);

    // A runner with its daemon built tells it the extension went, which is the
    // report made.
    [runner, bus] = await acquired();
    [daemon] = daemons.slice(-1);
    assert.equal(runner.reportExtensionGone(), true);
    assert.deepEqual(daemon.extensionStates, [false]);
    runner.shutdown();

    // A runner owning the name with no daemon built yet, the store still busy,
    // answers for the extension's report itself -- nobody else would answer
    // the call but this process, which exports nothing yet -- and one that
    // lost the name leaves it to the bus.
    {
        storage.busy = true;
        const settling = new context.Runner(settings);
        const settlingBus = buses.at(-1);

        assert.equal(settling.reportExtensionGone(), false);
        settlingBus.emit('name-acquired');
        await settle();
        assert.equal(settling._daemon ?? null, null);
        assert.equal(settling.reportExtensionGone(), true);
        settlingBus.emit('name-lost', false);
        assert.equal(settling.reportExtensionGone(), false);
        settling.shutdown();
        storage.busy = false;
        runSources('timeout');
    }

    // A runner acquiring the name meanwhile takes the lock and the passphrase
    // over, and settles only once the store is idle.
    storage.passphrases.length = 0;
    storage.unlocks = 0;
    [runner, bus] = await acquired();
    storage.busy = true;
    runner.shutdown();
    const successor = new context.Runner(settings);
    const checks = storage.migrationChecks;

    buses.at(-1).emit('name-acquired');
    await settle();
    assert.equal(storage.migrationChecks, checks);
    storage.busy = false;
    runSources('timeout');
    await successor.ready;
    assert.equal(storage.migrationChecks, checks + 1);
    assert.equal(storage.unlocks, 0);
    assert.deepEqual(storage.passphrases, []);
    assert.equal(sources.size, 0);
    // Nor does the wipe the first was owed outlive that: a reconnect of the
    // successor releasing the lock later keeps the passphrase.
    storage.busy = true;
    buses.at(-1).connected = false;
    buses.at(-1).emit('name-lost', true);
    storage.busy = false;
    runSources('timeout');
    assert.equal(storage.unlocks, 1);
    assert.deepEqual(storage.passphrases, []);
    successor.shutdown();

    // A storage concern queued behind another flushes the daemon it was asked
    // for only if that is still the one there: a reconnect meanwhile flushed
    // and disposed it already.
    [runner, bus] = await acquired();
    [daemon] = daemons.slice(-1);
    let unblock;
    let concerns = 0;

    runner._storageChain = new Promise(resolve => { unblock = resolve; });
    const queued = runner._reloadStorageAfter(() => ++concerns);

    bus.connected = false;
    bus.emit('name-lost', true);
    assert.equal(daemon.disposed, true);
    assert.equal(daemon.flushes, 1);
    unblock();
    await queued;
    assert.equal(concerns, 1);
    assert.equal(daemon.flushes, 1);
    runner.shutdown();

    // A daemon that could not be built stands the runner down, the name going
    // to the standalone daemon, rather than holding it with nothing behind it:
    // the prompt shut down and the passphrase wiped with the lock, as for a
    // takeover.
    failDaemon = true;
    // Only the name refused to the settling runner above was logged so far.
    assert.deepEqual(errors.splice(0), ['GPaste: could not acquire the D-Bus name']);
    storage.passphrases.length = 0;
    [runner, bus] = await acquired();
    failDaemon = false;
    assert.equal(errors.length, 1);
    errors.length = 0;
    assert.equal(runner._standDown, true);
    assert.equal(bus.unowned, 1);
    assert.equal(runner._prompt.shutDowns, 1);
    assert.deepEqual(storage.passphrases, [null]);
    runner.shutdown();

    // An export failing stands the runner down from inside the first
    // add_object (), as a takeover does, and nothing is exported after it.
    failExport = true;
    storage.passphrases.length = 0;
    [runner, bus] = await acquired();
    [daemon] = daemons.slice(-1);
    failExport = false;
    assert.equal(bus.objects.length, 1);
    assert.equal(runner._standDown, true);
    assert.equal(bus.unowned, 1);
    assert.equal(daemon.flushes, 1);
    assert.equal(daemon.disposed, true);
    assert.equal(runner._prompt.shutDowns, 1);
    assert.deepEqual(storage.passphrases, [null]);
    assert.equal(errors.length, 1);
    errors.length = 0;
    runner.shutdown();

    // Shut down while it waits for a store another runner rewrites: the wait
    // goes with it, rather than poll until the store is idle.
    const settled = storage.migrationChecks;

    storage.busy = true;
    runner = new context.Runner(settings);
    buses.at(-1).emit('name-acquired');
    await settle();
    const wait = runner._storageWait?.id;

    assert.equal(sources.get(wait)?.name, '[GPaste] storage idle');
    runner.shutdown();
    assert.equal(sources.has(wait), false);
    await settle();
    assert.equal(storage.migrationChecks, settled);
    storage.busy = false;
    runSources('timeout');

    // A re-exec asked for and shut down on before its idle runs takes the idle
    // with it.
    [runner, bus] = await acquired();
    [daemon] = daemons.slice(-1);
    daemon.handlers['reexecute-self']();
    assert.equal(countSources('idle'), 1);
    runner.shutdown();
    assert.equal(countSources('idle'), 0);

    // A re-exec the daemon asks for runs once, from an idle, however many times
    // it is asked meanwhile.
    [runner, bus] = await acquired();
    [daemon] = daemons.slice(-1);
    let migrations = 0;

    runner._migrateInPlace = () => ++migrations;
    daemon.handlers['reexecute-self']();
    daemon.handlers['reexecute-self']();
    assert.equal(countSources('idle'), 1);
    runSources('idle');
    await settle();
    assert.equal(migrations, 1);
    runner.shutdown();

    // A check of the store the daemon asks for runs once, from an idle,
    // between a flush and a reload, and what it found goes back to the daemon
    // that asked; a prompt that failed is reported in its place.
    [runner, bus] = await acquired();
    [daemon] = daemons.slice(-1);
    happened.length = 0;
    daemon.handlers['doctor']();
    daemon.handlers['doctor']();
    assert.equal(countSources('idle'), 1);
    runSources('idle');
    for (let i = 0; i < 10 && !daemon.doctored; ++i)
        await settle();
    assert.equal(daemon.flushes, 1);
    assert.equal(daemon.reloads, 1);
    assert.deepEqual(happened, ['flush', 'check', 'reload']);
    assert.deepEqual(daemon.doctored, [['Restricted the folder'], true]);

    storage.doctorError = 'The prompt failed';
    daemon.doctored = null;
    daemon.handlers['doctor']();
    runSources('idle');
    for (let i = 0; i < 10 && !daemon.doctored; ++i)
        await settle();
    storage.doctorError = null;
    // Spread: an array the runner builds is the vm context's, of another Array.
    assert.deepEqual([[...daemon.doctored[0]], daemon.doctored[1]], [['The prompt failed'], false]);

    // And to the daemon that asked, whichever is there once the check is done.
    const other = {flush() {}, reload_storage() {}, doctor_done() { other.doctored = true; }};

    storage.holdDoctor = true;
    daemon.doctored = null;
    daemon.handlers['doctor']();
    runSources('idle');
    for (let i = 0; i < 10 && !storage.heldDoctor; ++i)
        await settle();
    runner._daemon = other;
    storage.holdDoctor = false;
    storage.heldDoctor();
    for (let i = 0; i < 10 && !daemon.doctored; ++i)
        await settle();
    assert.deepEqual(daemon.doctored, [['Restricted the folder'], true]);
    assert.equal(other.doctored, undefined);
    runner._daemon = daemon;
    runner.shutdown();

    assert.deepEqual(errors, []);
    console.log('Daemon runner: stand-down, reconnect, shutdown and storage hand-over passed');
});
