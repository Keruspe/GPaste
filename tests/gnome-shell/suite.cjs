// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

// What the Node suites share, required from beside them.

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

module.exports = {Signals, runSuite};
