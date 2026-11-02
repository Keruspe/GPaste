// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

// Execute the shipped extension's enable () and disable () with doubles for the
// shell, the bus and libgpaste: which report of the extension going away is
// made, what a failed one logs, and the GPaste.Settings the indicator reads
// through the extension's `settings` getter. Not the shell's own extension
// machinery.
const assert = require('node:assert/strict');
const vm = require('node:vm');
const {loadShipped} = require('./suite.cjs');

const errors = [];
const calls = [];
let reply = null;
let indicator = null;
let runner = null;
let built = null;

class Extension {}

const context = vm.createContext({
    console: {error: message => errors.push(message)},
    Extension,
    Main: {
        sessionMode: {
            currentMode: 'user',
            connectObject(signal, handler, owner) { this.connected = owner; },
            disconnectObject(owner) { if (this.connected === owner) this.connected = null; },
        },
        panel: {
            get statusArea() { return {gpaste: indicator}; },
            addToStatusArea() {},
        },
    },
    Gio: {
        DBus: {
            session: {
                call(name, path, iface, method, params, type, flags, timeout, cancellable, callback) {
                    calls.push({name, path, iface, method, flags});
                    callback({call_finish() { if (reply) throw reply; }}, null);
                },
            },
        },
        // Spelt as Gio's own: a member it lacks reads as undefined there.
        DBusCallFlags: {NONE: 0, NO_AUTO_START: 1},
    },
    GLib: {Variant: class {}},
    GPaste: {
        BUS_NAME: 'bus-name',
        DAEMON_OBJECT_PATH: '/object/path',
        DAEMON_INTERFACE_NAME: 'interface-name',
        Client: {is_daemon_gone_error: e => e.gone},
        Settings: class { get_experimental_meta_daemon() { return !!runner; } },
    },
    GPasteDaemonRunner: class {
        constructor() { return runner; }
    },
    // What indicator.js reads at construction.
    GPasteIndicator: class {
        constructor(extension) {
            built = this;
            this.settings = extension.settings;
        }
    },
});
loadShipped(context, {ExtensionClass: 'GPasteExtension'});

// The indicator, which reports through its client when it has one.
function connectedIndicator() {
    return {
        reported: null,
        shutdown(extensionDisabled) { this.reported = extensionDisabled; return extensionDisabled; },
    };
}

// With the in-shell daemon told in process, the indicator reports nothing more.
{
    runner = {ready: new Promise(() => {}), reportExtensionGone: () => true, shutdown() {}};
    indicator = connectedIndicator();
    const extension = new context.ExtensionClass();

    extension.enable();
    extension.disable();
    assert.equal(indicator.reported, false);
    assert.equal(calls.length, 0);
    assert.equal(context.Main.sessionMode.connected, null);
}

// Without a daemon of its own nor an indicator to report, the extension calls
// the daemon's name itself, read off libgpaste; a daemon that is not there --
// whichever way the bus says so -- is no error.
{
    runner = null;
    indicator = null;
    reply = {gone: true, message: 'NameHasNoOwner'};
    const extension = new context.ExtensionClass();

    extension.enable();
    extension.disable();
    // Starting no daemon to tell it the extension went.
    assert.deepEqual(calls.at(-1), {name: 'bus-name', path: '/object/path', iface: 'interface-name', method: 'ReportExtensionState', flags: 1});
    assert.deepEqual(errors, []);

    reply = {gone: false, message: 'refused'};
    extension.enable();
    extension.disable();
    assert.deepEqual(errors, ['GPaste: refused']);
}

// The indicator it builds reads the extension's GPaste.Settings, the one of the
// process, for as long as the extension is enabled.
{
    runner = null;
    indicator = null;
    reply = null;
    const extension = new context.ExtensionClass();

    extension.enable();
    assert.ok(extension.settings instanceof context.GPaste.Settings);
    assert.equal(built.settings, extension.settings);

    extension.disable();
    assert.equal(extension.settings, null);
}

console.log('extension tests passed');
