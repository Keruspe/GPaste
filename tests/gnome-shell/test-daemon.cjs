// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

// Execute the shipped in-shell daemon runner with doubles for the bus, the
// daemon, the storage and the main loop: what standing down, a reconnect and a
// shutdown leave behind, and how a runner waits on a store another is still
// rewriting. Not the daemon itself, which the C suites cover.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const errors = [];
// Every main loop source, by id: what runs it is the test.
const sources = new Map();
let nextSource = 1;
const storage = {busy: false, unlocks: 0, passphrases: [], migrationChecks: 0};
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

function settle() {
    return new Promise(resolve => setImmediate(resolve));
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
        daemons.push(this);
    }

    expire_password() {}
    flush() { assert.equal(this.disposed, false); ++this.flushes; }
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
        storage_migration_needed: () => {
            ++storage.migrationChecks;
            return false;
        },
        storage_decryption_needed: () => false,
    },
    GPasteShellPrompt: class {
        constructor() { this.shutDowns = 0; }
        shutdown() { ++this.shutDowns; }
    },
    global: {display: {get_selection: () => ({})}},
});
const source = fs.readFileSync(process.argv[2], 'utf8')
    .replace(/^import [^;]*;\n/gm, '').replace('export class ', 'class ');
vm.runInContext(`${source}\nthis.Runner = GPasteDaemonRunner;`, context);

const settings = {reload() {}};

// A runner whose bus has acquired the name and built the daemon.
async function acquired() {
    const runner = new context.Runner(settings);
    const bus = buses.at(-1);

    bus.emit('name-acquired');
    await runner.ready;
    return [runner, bus];
}

// Node exits 0 once nothing is left to run, so an await that never settles
// would end the suite there, every assertion after it skipped: only reaching
// the end counts as passing.
let finished = false;
process.on('exit', () => {
    if (!finished) {
        console.error('Daemon runner: the suite stopped on a promise that never settled');
        process.exitCode = 1;
    }
});

(async () => {
    // Another daemon taking the name over: the one built here is flushed and
    // disposed rather than left to the collector, the bus let go of, the
    // prompt shut down, and the passphrase wiped with the lock.
    let [runner, bus] = await acquired();
    let [daemon] = daemons.slice(-1);

    assert.equal(bus.objects.length, 2);
    bus.emit('name-lost', true);
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
    errors.length = 0;
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
    const waits = () => [...sources.values()].filter(s => s.name === '[GPaste] wait for the storage to be idle').length;

    assert.equal(waits(), 1);
    runner.shutdown();
    assert.equal(waits(), 0);
    await settle();
    assert.equal(storage.migrationChecks, settled);
    storage.busy = false;
    runSources('timeout');

    // A re-exec the daemon asks for runs once, from an idle, however many times
    // it is asked meanwhile.
    [runner, bus] = await acquired();
    [daemon] = daemons.slice(-1);
    let migrations = 0;

    runner._migrateInPlace = () => ++migrations;
    daemon.handlers['reexecute-self']();
    daemon.handlers['reexecute-self']();
    assert.equal([...sources.values()].filter(s => s.kind === 'idle').length, 1);
    runSources('idle');
    await settle();
    assert.equal(migrations, 1);
    runner.shutdown();

    assert.deepEqual(errors, []);
    finished = true;
    console.log('Daemon runner: stand-down, reconnect, shutdown and storage hand-over passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
