// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

// What the Node suites share, required from beside them.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

// Signals as the shell's actors carry them, enough to emit one by hand.
class Signals {
    connect(signal, handler) {
        (this._handlers ??= []).push({signal, handler});
        return this._handlers.length;
    }

    emit(signal, ...args) {
        for (const {signal: name, handler} of this._handlers ?? []) {
            if (name === signal)
                handler(this, ...args);
        }
    }
}

// Runs @body, the suite @name says it is. Node exits 0 once nothing is left to
// run, so an await that never settles would end the suite there, every
// assertion after it skipped: only the body settling counts as having run, a
// failed assertion being reported as itself rather than as a hang.
function runSuite(name, body) {
    let finished = false;

    process.on('exit', () => {
        if (!finished) {
            console.error(`${name}: the suite stopped on a promise that never settled`);
            process.exitCode = 1;
        }
    });

    body().catch(error => {
        console.error(error);
        process.exitCode = 1;
    }).finally(() => {
        finished = true;
    });
}

// Lets every reaction already queued run, and those they queue in turn: a
// reply resolved by hand reaches what awaits it.
function settle() {
    return new Promise(resolve => setImmediate(resolve));
}

// The shipped file the suite was handed, or @file beside it.
function shipped(file) {
    return file === undefined ? process.argv[2] : path.join(path.dirname(process.argv[2]), file);
}

// Runs the shipped module @file (shipped ()) in @context, its imports left to
// what @context defines and its exports declared plainly, and sets each name of
// @exports on @context to the expression, in the module's scope, it maps to.
function loadShipped(context, exports, file) {
    const source = fs.readFileSync(shipped(file), 'utf8')
        .replace(/^import [^;]*;\n/gm, '').replace(/^export (default )?/gm, '');
    const exported = Object.entries(exports).map(([name, value]) => `this.${name} = ${value};`);

    vm.runInContext([source, ...exported].join('\n'), context);
}

// Runs the function @name of the shipped @file in @context, @file sitting beside
// the shipped file the suite was handed: a helper of a module the suite does not
// load whole.
function runShipped(context, file, name) {
    const source = fs.readFileSync(shipped(file), 'utf8');
    const [found] = source.match(new RegExp(`^export function ${name}\\b[^]*?^}\n`, 'm')) ?? [];

    if (!found)
        throw new Error(`${file} has no function ${name}`);

    vm.runInContext(found.replace('export ', ''), context);
}

module.exports = {Signals, loadShipped, runShipped, runSuite, settle, shipped};
