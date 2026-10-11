// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

// Execute the shipped extension with doubles for the shell's panel and session
// mode: the indicator it builds reads the extension's GPaste.Settings through
// its `settings` getter, the one GPaste.Settings of the process, for as long as
// the extension is enabled. Not Shell rendering.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

class Settings {
    get_experimental_meta_daemon() { return false; }
}

const statusArea = {};
const context = vm.createContext({
    console,
    Main: {
        sessionMode: {currentMode: 'user', connect: () => 1, disconnect() {}},
        panel: {
            statusArea,
            addToStatusArea(name, indicator) { statusArea[name] = indicator; },
        },
    },
    Extension: class {},
    Gio: {},
    GLib: {},
    GPaste: {Settings},
    GPasteDaemonRunner: class {},
    // What indicator.js reads at construction.
    GPasteIndicator: class {
        constructor(extension) {
            this.settings = extension.settings;
        }

        shutdown() {
            delete statusArea.gpaste;
            return true;
        }
    },
});
const source = fs.readFileSync(process.argv[2], 'utf8')
    .replace(/^import [^;]*;\n/gm, '').replace(/^export default class /m, 'class ');
vm.runInContext(`${source}\nthis.Extension = GPasteExtension;`, context);

const extension = new context.Extension();

extension.enable();
assert.ok(extension.settings instanceof Settings);
assert.equal(statusArea.gpaste.settings, extension.settings);

extension.disable();
assert.equal(statusArea.gpaste, undefined);
assert.equal(extension.settings, null);

console.log('Extension: settings getter passed');
