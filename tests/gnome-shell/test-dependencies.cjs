// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

// Execute the shipped helpers every other suite stands in for: what a failure
// nothing waits on is said with and at which level, and what a reply a newer
// request replaced is read as.
const assert = require('node:assert/strict');
const vm = require('node:vm');
const {loadShipped, runSuite} = require('./suite.cjs');

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
loadShipped(context, {helpers: '{logFailure, debugFailure, replaceCancellable, awaitReply, daemonGone, widthWithoutRevealed}'});

const {logFailure, debugFailure, replaceCancellable, awaitReply, daemonGone, widthWithoutRevealed} = context.helpers;

runSuite('Dependencies', async () => {
    // A call failing as its daemon left is told from one failing otherwise, and
    // one failing with no GLib.Error at all is that second kind, not a
    // TypeError of its own.
    assert.equal(daemonGone(new GLibError('dbus', 'gone', 'NoReply')), true);
    assert.equal(daemonGone(new GLibError('dbus', 'other', 'Refused')), false);
    assert.equal(daemonGone(new Error('a bug down the call')), false);

    // A row's read failing is said at debug level, and nowhere else; anything
    // else thrown filling the row is an error.
    debugFailure(new GLibError('dbus', 'gone', 'gone'));
    assert.deepEqual(said.debug, ['GPaste: gone']);
    assert.deepEqual(said.error, []);
    debugFailure(new TypeError('a bug'));
    assert.equal(said.error.pop().message, 'a bug');
    assert.deepEqual(said.debug, ['GPaste: gone']);

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

    // A row asks for its width less each button it shows only while looked at,
    // with the spacing the box puts before it -- a hidden one is left out of
    // the box already -- and never for less than its minimum.
    {
        const row = {layout_manager: {spacing: 6}};
        const button = (visible, width) => ({visible, get_preferred_width: () => [width, width]});

        assert.deepEqual([...widthWithoutRevealed(row, [30, 544], [button(true, 16), button(true, 16)], -1)], [30, 500]);
        assert.deepEqual([...widthWithoutRevealed(row, [30, 522], [button(true, 16), button(false, 16)], -1)], [30, 500]);
        assert.deepEqual([...widthWithoutRevealed(row, [30, 40], [button(true, 16)], -1)], [30, 30]);
    }

    console.log('Dependencies: failure levels, replaced requests and their replies, and revealed widths passed');
});
