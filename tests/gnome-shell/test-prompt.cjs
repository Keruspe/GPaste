// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

// Execute the shipped GNOME Shell prompt with doubles for St, the shell's
// dialogs and the session mode: what a locked session hides and holds back,
// which theme classes the passphrase's strength meter wears, and what a
// passphrase asked for other histories says and offers. Not Shell rendering.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {Signals} = require('./suite.cjs');

const errors = [];
const dialogs = [];

class Actor extends Signals {
    constructor(params = {}) {
        super();
        Object.assign(this, params);
        this.children = [];
        this.visible = true;
        this.clutter_text = {connect() {}};
    }

    add_child(child) { this.children.push(child); }

    get_text() { return ''; }
}

class CheckBox extends Actor {
    constructor(label) {
        super();
        this.label = label;
    }
}

class ModalDialog extends Actor {
    constructor(params) {
        super(params);
        this.contentLayout = new Actor();
        this.opened = false;
        dialogs.push(this);
    }

    addButton(params) {
        const button = new Actor(params);

        (this.buttons ??= []).push(button);
        return button;
    }
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
    CheckBox: {CheckBox},
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
            pwquality_available: () => true,
            passphrase_is_complete: () => false,
            storage_label: storage => `storage ${storage}`,
            can_import: () => false,
            backend_changes: () => false,
            cleanup_warns: () => false,
            remember_state: () => [false, true],
            // What it was asked, to be told apart from anything prompt.js
            // would decide for itself.
            remember_answer: (switchOn, canForget) => `answer(${switchOn}, ${canForget})`,
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
        get_description: () => null,
        // What the C side works out, marked as remember_answer () is.
        get_shown_description() { return `shown(${this.get_description() ?? this.get_confirm()})`; },
        get_accept_label() { return `accept(${this.get_confirm()})`; },
        get_error_message: () => null,
        get_remember: () => 0,
        offers_remember: () => false,
        reply_passphrase(passphrase, remember) {
            this.answers.push(passphrase);
            this.remember = remember;
        },
        dismiss() { this.answers.push('dismissed'); },
    };
}

function confirmRequest() {
    return Object.assign(request(), {
        get_title: () => 'Delete?',
        get_description: () => 'Images go',
        get_accept_label: () => 'Delete',
        reply_confirm() { this.answers.push('confirmed'); },
    });
}

// The actors under @actor, depth first.
function* actors(actor) {
    for (const child of actor.children ?? []) {
        yield child;
        yield* actors(child);
    }
}

// A migration offering the one backend in use, and keeping it.
function migrationRequest() {
    return Object.assign(request(), {get_current: () => 0, get_offered: () => [0], can_keep: () => true});
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

// A confirmation shows what its caller worded, Cancel being its default and
// Escape's, and answers yes from its other button alone: closed, it declines.
{
    const accepted = confirmRequest();

    prompt.vfunc_confirm(accepted);

    const dialog = dialogs.at(-1);
    const [message] = dialog.contentLayout.children;

    assert.equal(message.title, 'Delete?');
    assert.equal(message.description, 'Images go');
    assert.deepEqual(dialog.buttons.map(button => [button.label, Boolean(button.default), button.key]),
        [['CANCEL', true, 0], ['Delete', false, undefined]]);
    dialog.buttons[1].action();
    assert.deepEqual(accepted.answers, ['confirmed']);

    const declined = confirmRequest();

    prompt.vfunc_confirm(declined);
    dialogs.at(-1).buttons[0].action();
    assert.deepEqual(declined.answers, ['dismissed']);
}

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
assert.equal(dialogs.length, 5);

// Asked anything once shut down, unlocked or not -- the storage layer asking
// again as a dismissal unblocks it -- it puts no dialog up: a question is
// dismissed and a report goes to the log.
for (const locked of [false, true]) {
    sessionMode.update(locked);

    const late = request();
    const lateMigration = migrationRequest();
    const lateConfirm = confirmRequest();
    const shownBefore = dialogs.length;

    errors.length = 0;
    prompt.vfunc_passphrase(late);
    prompt.vfunc_migration(lateMigration);
    prompt.vfunc_confirm(lateConfirm);
    prompt.vfunc_report('Late', 'Report');
    assert.equal(dialogs.length, shownBefore);
    assert.deepEqual(late.answers, ['dismissed']);
    assert.deepEqual(lateMigration.answers, ['dismissed']);
    assert.deepEqual(lateConfirm.answers, ['dismissed']);
    assert.deepEqual(errors, ['GPaste: Late: Report']);
}

// Shut down with a dialog up, unlocked: it is closed, its request answered as
// a closed dialog answers, and that answer asking again, as the storage layer
// can, finds the prompt shut down already rather than putting a dialog up for
// an extension gone.
{
    sessionMode.update(false);

    const closing = new context.Prompt();
    const upNow = request();
    const askedAgain = migrationRequest();

    upNow.reply_passphrase = function (passphrase) {
        this.answers.push(passphrase);
        closing.vfunc_migration(askedAgain);
    };
    closing.vfunc_passphrase(upNow);

    const shownBefore = dialogs.length;

    closing.shutdown();
    assert.deepEqual(upNow.answers, [null]);
    assert.deepEqual(askedAgain.answers, ['dismissed']);
    assert.equal(dialogs.length, shownBefore);
}

// The migration dialog names "Keep as is" only where it offers it: not for a
// backend this build cannot open, which is on no list.
const migrationPrompt = new context.Prompt();

sessionMode.update(false);

for (const [current, keep, description] of [[0, true, 'MIGRATION_DESCRIPTION'], [9, false, 'MIGRATION_DESCRIPTION_UNKEPT']]) {
    const migration = {
        get_current: () => current,
        get_offered: () => [0, 1],
        can_keep: () => keep,
        reply_migration() {},
        dismiss() {},
    };
    const shownBefore = dialogs.length;

    migrationPrompt.vfunc_migration(migration);
    assert.equal(dialogs.length, shownBefore + 1);

    const [migrationContent] = dialogs.at(-1).contentLayout.children;

    assert.equal(migrationContent.description, description);
    assert.equal(dialogs.at(-1).buttons.some(button => button.label === 'MIGRATION_KEEP'), keep);
    dialogs.at(-1).close();
}

migrationPrompt.shutdown();

// A passphrase asked for other histories says what it is wanted for, and
// offers no switch to remember it; the history's own offers one whenever the
// request says to.
sessionMode.update(false);
const unlocked = new context.Prompt();
const other = Object.assign(request(), {
    get_confirm: () => false,
    get_description: () => 'Other histories',
});
const own = Object.assign(request(), {
    get_confirm: () => false,
    offers_remember: () => true,
});

unlocked.vfunc_passphrase(other);
unlocked.vfunc_passphrase(own);

const [otherContent] = dialogs.at(-2).contentLayout.children;
const [ownContent] = dialogs.at(-1).contentLayout.children;
const switches = content => [...actors(content)].filter(actor => actor instanceof CheckBox);

assert.equal(otherContent.description, 'shown(Other histories)');
assert.equal(switches(otherContent).length, 0);
assert.equal(ownContent.description, 'shown(false)');
assert.equal(dialogs.at(-1)._okButton.label, 'accept(false)');
assert.deepEqual(switches(ownContent).map(box => box.label), ['REMEMBER']);
assert.ok([...actors(ownContent)].some(actor => actor.text === 'REMEMBER_SUBTITLE'));

// What the switch answers is the C side's, asked with where it was left and
// whether off means forgetting; without one, it is asked with neither.
switches(ownContent)[0].checked = true;
dialogs.at(-1)._onOk();
assert.equal(own.remember, 'answer(true, true)');
dialogs.at(-2)._onOk();
assert.equal(other.remember, 'answer(false, false)');
dialogs.slice(-2).forEach(dialog => dialog.close());

// Both prompts say one thing: every word one shows, the other shows too.
const words = (file, pattern) => new Set([...fs.readFileSync(file, 'utf8').matchAll(pattern)].map(m => m[1]));
const shellWords = words(process.argv[2], /PromptText\.([A-Z_]+)/g);
const adwWords = words(path.join(path.dirname(process.argv[2]), '../daemon/gpaste-prompt-adw.c'), /G_PASTE_PROMPT_TEXT_([A-Z_]+)/g);

assert.deepEqual([...adwWords].filter(word => !shellWords.has(word)), []);
assert.deepEqual([...shellWords].filter(word => !adwWords.has(word)), []);

console.log('Prompt: lock screen, strength meter, migration description, other histories and wording passed');
