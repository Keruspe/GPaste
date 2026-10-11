// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

// Execute the shipped helpers every other suite stands in for: what a failure
// nothing waits on is said with and at which level, and what a reply a newer
// request replaced is read as.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const said = {error: [], debug: []};

// GLib.Error as GJS raises it, enough to be matched.
class GLibError extends Error {
    constructor(domain, code, message) {
        super(message);
        this.domain = domain;
        this.code = code;
    }

    matches(domain, code) { return this.domain === domain && this.code === code; }
}

class Cancellable {
    cancel() { this.cancelled = true; }
    is_cancelled() { return !!this.cancelled; }
}

const IOErrorEnum = {CANCELLED: 19, FAILED: 0};
const context = vm.createContext({
    console: {error: e => said.error.push(e), debug: message => said.debug.push(message)},
    Gio: {_promisify() {}, Cancellable, IOErrorEnum},
    GLib: {Error: GLibError},
    // As the binding: anything but a GLib.Error is refused with a TypeError.
    GPaste: {
        Client: {
            prototype: {},
            is_daemon_gone_error: e => {
                if (!(e instanceof GLibError))
                    throw new TypeError('Object is not a subclass of GLib_Error');
                return e.code === 'gone';
            },
        },
    },
    Config: {LIBMUTTER_API_VERSION: '18'},
    gi: {require() {}},
});
const source = fs.readFileSync(process.argv[2], 'utf8')
    .replace(/^import [^;]*;\n/gm, '').replace(/^export /gm, '');
vm.runInContext(`${source}\nthis.helpers = {logFailure, debugFailure, replaceCancellable, awaitReply, daemonGone};`, context);

const {logFailure, debugFailure, replaceCancellable, awaitReply, daemonGone} = context.helpers;

(async () => {
    // A call failing as its daemon left is told from one failing otherwise, and
    // one failing with no GLib.Error at all is that second kind, not a
    // TypeError of its own.
    assert.equal(daemonGone(new GLibError('dbus', 'gone', 'NoReply')), true);
    assert.equal(daemonGone(new GLibError('dbus', 'other', 'Refused')), false);
    assert.equal(daemonGone(new Error('a bug down the call')), false);

    // A row's read failing is said at debug level, and nowhere else.
    debugFailure(new Error('gone'));
    assert.deepEqual(said.debug, ['GPaste: gone']);
    assert.deepEqual(said.error, []);

    // A call nothing waits on is finished, and a refusal said as an error.
    const client = {
        done_finish(result) {
            if (result === 'refused')
                throw new Error('refused');
        },
    };
    logFailure('done_finish')(client, 'fine');
    assert.deepEqual(said.error, []);
    logFailure('done_finish')(client, 'refused');
    assert.equal(said.error.length, 1);
    assert.equal(said.error[0].message, 'refused');

    // The request replaced is cancelled, its successor not.
    const first = replaceCancellable(null);
    const second = replaceCancellable(first);
    assert.equal(first.is_cancelled(), true);
    assert.equal(second.is_cancelled(), false);

    // A reply is wanted while its cancellable is not cancelled, one cancelled
    // landing all the same is not, nor is a call that failed as cancelled; any
    // other failure is the caller's, a plain exception included.
    const live = new Cancellable();
    assert.deepEqual([...await awaitReply(live, Promise.resolve(3))], [true, 3]);
    const replaced = new Cancellable();
    replaced.cancel();
    assert.deepEqual([...await awaitReply(replaced, Promise.resolve(3))], [false, 3]);
    assert.deepEqual([...await awaitReply(live, Promise.reject(new GLibError(IOErrorEnum, IOErrorEnum.CANCELLED, 'cancelled')))],
        [false, null]);
    await assert.rejects(awaitReply(live, Promise.reject(new GLibError(IOErrorEnum, IOErrorEnum.FAILED, 'failed'))), /failed/);
    await assert.rejects(awaitReply(live, Promise.reject(new TypeError('broken'))), TypeError);

    console.log('Dependencies: failure levels, replaced requests and their replies passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
