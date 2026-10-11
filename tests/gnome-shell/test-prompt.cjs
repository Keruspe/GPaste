// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

// Execute the shipped GNOME Shell prompt with doubles for St, the shell's
// dialogs and the session mode: what a locked session hides and holds back,
// and which theme classes the passphrase's strength meter wears. Not Shell
// rendering.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const errors = [];
const dialogs = [];

// Signals as the shell's actors carry them, enough to emit one by hand.
class Actor {
    constructor(params = {}) {
        Object.assign(this, params);
        this.children = [];
        this.visible = true;
        this.clutter_text = {connect() {}};
    }

    add_child(child) { this.children.push(child); }

    connect(signal, handler) {
        (this._handlers ??= []).push({signal, handler});
    }

    emit(signal) {
        for (const {signal: name, handler} of this._handlers ?? []) {
            if (name === signal)
                handler(this);
        }
    }

    get_text() { return ''; }
}

class ModalDialog extends Actor {
    constructor(params) {
        super(params);
        this.contentLayout = new Actor();
        this.opened = false;
        dialogs.push(this);
    }

    addButton(params) { return new Actor(params); }
    setInitialKeyFocus() {}
    open() { this.opened = true; return true; }
    close() { this.emit('closed'); this.destroy(); }
    destroy() { this.emit('destroy'); }
}

const sessionMode = {
    isLocked: false,
    connectObject(signal, handler) { this.handler = handler; },
    disconnectObject() { this.handler = null; },
    update(locked) {
        this.isLocked = locked;
        this.handler?.();
    },
};

const context = vm.createContext({
    console: {error: message => errors.push(message)},
    BarLevel: {BarLevel: Actor},
    CheckBox: {CheckBox: Actor},
    Dialog: {MessageDialogContent: Actor},
    Main: {sessionMode},
    ModalDialog: {ModalDialog},
    ShellEntry: {addContextMenu() {}},
    Clutter: {ActorAlign: {CENTER: 0}, Orientation: {VERTICAL: 1}, KEY_Escape: 0},
    GLib: {PRIORITY_DEFAULT: 0, SOURCE_REMOVE: false, timeout_add: () => 1, Source: {remove() {}, set_name_by_id() {}}},
    GObject: {registerClass: (...args) => args.at(-1), Object: class {}},
    Pango: {EllipsizeMode: {NONE: 0}},
    St: {Label: Actor, BoxLayout: Actor, PasswordEntry: Actor},
    GPasteDaemon: {
        Prompt: {
            keyring_available: () => false,
            pwquality_available: () => true,
            passphrase_is_complete: () => false,
            storage_label: storage => `storage ${storage}`,
            can_import: () => false,
            backend_changes: () => false,
        },
        PromptText: new Proxy({}, {get: (target, name) => name}),
        StorageRemember: {UNCHANGED: 0, YES: 1, NO: 2},
        prompt_text: text => text,
    },
});
const source = fs.readFileSync(process.argv[2], 'utf8')
    .replace(/^import [^;]*;\n/gm, '').replace(/^export const /gm, 'const ');
vm.runInContext(`${source}\nthis.Prompt = GPasteShellPrompt;`, context);

function request() {
    return {
        answers: [],
        get_confirm: () => true,
        get_error_message: () => null,
        get_remember: () => 0,
        reply_passphrase(passphrase) { this.answers.push(passphrase); },
        dismiss() { this.answers.push('dismissed'); },
    };
}

// The actors under @actor, depth first.
function* actors(actor) {
    for (const child of actor.children ?? []) {
        yield child;
        yield* actors(child);
    }
}

const prompt = new context.Prompt();

// A new passphrase is rated on a meter the shell's theme draws -- a BarLevel
// as a 'slider', its own 'barlevel' class having no height and no colours --
// with its hint as a dialog's description.
const asked = request();

prompt.vfunc_passphrase(asked);
const [shown] = dialogs;
const [content] = shown.contentLayout.children;
const meter = [...actors(content)].find(actor => actor.style_class === 'slider');

assert.equal(shown.opened, true);
assert.ok(meter);
assert.ok([...actors(content)].some(actor => actor.style_class === 'message-dialog-description'));

// Locking hides it, the shield's grab over its own, and unlocking shows it
// again.
sessionMode.update(true);
assert.equal(shown.visible, false);
sessionMode.update(false);
assert.equal(shown.visible, true);
shown.close();
assert.deepEqual(asked.answers, [null]);

// A question asked while locked waits for the unlock, as does a report.
sessionMode.update(true);
const waiting = request();

prompt.vfunc_passphrase(waiting);
prompt.vfunc_report('Title', 'Message');
assert.equal(dialogs.length, 1);
sessionMode.update(false);
assert.equal(dialogs.length, 3);
assert.ok(dialogs.slice(1).every(dialog => dialog.opened));
dialogs.slice(1).forEach(dialog => dialog.close());

// Shut down while one waits: the question is dismissed rather than left
// pending, the report goes to the log, and the session is no longer watched.
sessionMode.update(true);
const abandoned = request();

prompt.vfunc_passphrase(abandoned);
prompt.vfunc_report('Title', 'Message');
prompt.shutdown();
assert.deepEqual(abandoned.answers, ['dismissed']);
assert.deepEqual(errors, ['GPaste: Title: Message']);
assert.equal(sessionMode.handler, null);
assert.equal(dialogs.length, 3);

// The migration dialog names "Keep as is" only where it offers it: not for a
// backend this build cannot open, which is on no list.
const migrationPrompt = new context.Prompt();

sessionMode.update(false);

for (const [current, description] of [[0, 'MIGRATION_DESCRIPTION'], [9, 'MIGRATION_DESCRIPTION_UNKEPT']]) {
    const migration = {
        get_current: () => current,
        get_offered: () => [0, 1],
        reply_migration() {},
        dismiss() {},
    };
    const shownBefore = dialogs.length;

    migrationPrompt.vfunc_migration(migration);
    assert.equal(dialogs.length, shownBefore + 1);

    const [migrationContent] = dialogs.at(-1).contentLayout.children;

    assert.equal(migrationContent.description, description);
    dialogs.at(-1).close();
}

migrationPrompt.shutdown();

console.log('Prompt: lock screen, strength meter and migration description passed');
