// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import * as BarLevel from 'resource:///org/gnome/shell/ui/barLevel.js';
import * as CheckBox from 'resource:///org/gnome/shell/ui/checkBox.js';
import * as Dialog from 'resource:///org/gnome/shell/ui/dialog.js';
import * as ModalDialog from 'resource:///org/gnome/shell/ui/modalDialog.js';
import * as ShellEntry from 'resource:///org/gnome/shell/ui/shellEntry.js';

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import Pango from 'gi://Pango';
import St from 'gi://St';

import GPasteDaemon from 'gi://GPasteDaemon?version=1';

// The gnome-shell half of the GPastePrompt contract, the St counterpart of the
// standalone daemon's GPastePromptAdw. gnome-shell can run neither gtk_init nor
// adw_init, which is why the storage questions used to be shelled out to the
// gpaste-storage helper; answering them here is what lets the in-shell daemon
// call the very same in-process code the standalone one does.
//
// Everything about *what the answers mean* stays in the C storage layer: these
// dialogs only collect a passphrase, or a backend and two toggles, and hand
// them to the request.

// A wrong passphrase is reported by re-asking with an error message, so the
// dialog never has to know whether the one it collected was any good.
const PassphraseDialog = GObject.registerClass(
class GPastePassphraseDialog extends ModalDialog.ModalDialog {
    constructor(request) {
        super({styleClass: 'prompt-dialog'});

        this._request = request;
        this._answered = false;

        const confirm = request.get_confirm();
        const errorMessage = request.get_error_message();

        const content = new Dialog.MessageDialogContent({
            title: _('Encrypted history'),
            description: confirm
                // Translators: shown when choosing a new passphrase for the encrypted history
                ? _('If you forget this passphrase, your stored history cannot be recovered.')
                // Translators: shown when unlocking the encrypted history at startup
                : _('Enter the passphrase to unlock your clipboard history.'),
        });

        this._entry = this._addEntry(content, _('Passphrase'));
        this._confirmEntry = confirm ? this._addEntry(content, _('Confirm passphrase')) : null;

        if (confirm)
            this._addStrengthMeter(content);

        // Only offer to remember it when the build can: without libsecret there
        // is no keyring to remember it in, and the C side reports that by never
        // being built with it — mirrored here by the switch simply not existing.
        this._remember = null;
        if (GPasteDaemon.Prompt.keyring_available()) {
            // Where the switch starts, and whether leaving it off means
            // anything, are two different questions with one subtle rule
            // between them — answered C-side so the two backends cannot drift,
            // and in one keyring round trip rather than two.
            const [startsOn, canForget] =
                GPasteDaemon.Prompt.remember_state(request.get_remember());

            this._remembered = canForget;

            this._remember = new CheckBox.CheckBox(_('Remember this passphrase'));
            this._remember.checked = startsOn;
            content.add_child(this._remember);
        }

        this._errorLabel = new St.Label({style_class: 'prompt-dialog-error-label'});
        this._errorLabel.clutter_text.ellipsize = Pango.EllipsizeMode.NONE;
        this._errorLabel.clutter_text.line_wrap = true;
        this._errorLabel.visible = !!errorMessage;
        if (errorMessage)
            this._errorLabel.text = errorMessage;
        content.add_child(this._errorLabel);

        this.contentLayout.add_child(content);

        this._okButton = this.addButton({
            label: confirm ? _('Set passphrase') : _('Unlock'),
            action: () => this._onOk(),
            default: true,
        });
        this.addButton({
            label: _('Cancel'),
            action: () => this.close(),
            key: Clutter.KEY_Escape,
        });

        this.setInitialKeyFocus(this._entry);
        this._updateOk();

        // Whichever way the dialog goes away, the request gets exactly one
        // answer: replying already marked it, so these only bite on a real
        // dismissal. Both are connected because a dialog can be destroyed
        // without being closed, and 'closed' alone would then strand the
        // request — and with it the daemon waiting on the storage to settle.
        this.connect('closed', () => this._dismiss());
        this.connect('destroy', () => this._dismiss());
    }

    _dismiss() {
        this._answer(null, GPasteDaemon.StorageRemember.UNCHANGED);
    }

    _addEntry(content, hintText) {
        const entry = new St.PasswordEntry({
            style_class: 'prompt-dialog-password-entry',
            hint_text: hintText,
            can_focus: true,
            x_expand: true,
        });

        entry.clutter_text.connect('activate', () => this._onActivate());
        entry.clutter_text.connect('text-changed', () => this._onTextChanged(entry));
        ShellEntry.addContextMenu(entry);
        content.add_child(entry);

        return entry;
    }

    // The same libpwquality rating the Adwaita prompt shows, scored C-side:
    // libpwquality is not introspectable, so the level and its hint both come
    // from g_paste_prompt_passphrase_strength().
    _addStrengthMeter(content) {
        // Built without libpwquality there is nothing to rate with, and a meter
        // stuck at zero reads as a verdict rather than an absence. Say so
        // instead, as the libadwaita backend does.
        if (!GPasteDaemon.Prompt.pwquality_available()) {
            content.add_child(new St.Label({
                style_class: 'prompt-dialog-description',
                text: _('Passphrase strength rating is not available in this build'),
            }));
            return;
        }

        const box = new St.BoxLayout({style_class: 'gpaste-passphrase-strength'});

        // BarLevel is normalized to 0..1, so the 0-4 rating is scaled into it.
        this._strengthLevel = new BarLevel.BarLevel({x_expand: true});
        this._strengthHint = new St.Label({style_class: 'prompt-dialog-description'});

        box.add_child(this._strengthLevel);
        box.add_child(this._strengthHint);
        content.add_child(box);
    }

    _onTextChanged(entry) {
        // The error flags the previous wrong attempt; clear it as soon as the
        // passphrase is amended so it does not bleed into the next try.
        this._errorLabel.visible = false;

        // Only re-rate when the passphrase itself changed: typing the
        // confirmation rates a string that did not move, and rating means a
        // cracklib dictionary pass — here, on the compositor's own thread.
        if (this._strengthLevel && entry === this._entry) {
            const [level, hint] = GPasteDaemon.Prompt.passphrase_strength(this._entry.get_text());

            this._strengthLevel.value = level / GPasteDaemon.PROMPT_STRENGTH_MAX;
            this._strengthHint.text = hint ?? '';
        }

        this._updateOk();
    }

    _updateOk() {
        this._okButton.reactive = this._okButton.can_focus = this._isComplete();
    }

    // The rule lives in C so the two prompt backends cannot come to disagree
    // about what counts as an answer.
    _isComplete() {
        return GPasteDaemon.Prompt.passphrase_is_complete(
            this._entry.get_text(),
            this._confirmEntry ? this._confirmEntry.get_text() : null);
    }

    _onActivate() {
        if (this._isComplete())
            this._onOk();
    }

    _onOk() {
        let remember = GPasteDaemon.StorageRemember.UNCHANGED;

        // Only report the choice: the keyring is written by whoever established
        // that this passphrase is the right one, which cannot be known here.
        // Turning the switch off is a request to forget; finding it off and
        // leaving it there is not.
        if (this._remember?.checked)
            remember = GPasteDaemon.StorageRemember.YES;
        else if (this._remembered)
            remember = GPasteDaemon.StorageRemember.NO;

        this._answer(this._entry.get_text(), remember);
        this.close();
    }

    _answer(passphrase, remember) {
        if (this._answered)
            return;

        this._answered = true;
        this._request.reply_passphrase(passphrase, remember);
    }
});

const MigrationDialog = GObject.registerClass(
class GPasteMigrationDialog extends ModalDialog.ModalDialog {
    constructor(request) {
        super({styleClass: 'prompt-dialog'});

        this._request = request;
        this._answered = false;

        this._current = request.get_current();
        this._offered = request.get_offered();
        this._chosen = this._current;

        const content = new Dialog.MessageDialogContent({
            title: _('Storage migration'),
            description: _('Choose where GPaste should store your clipboard history. Nothing is kept on disk unless you pick a storing backend here.'),
        });

        this._backendButtons = new Map();

        const backends = new St.BoxLayout({
            orientation: Clutter.Orientation.VERTICAL,
            style_class: 'gpaste-storage-backends',
        });

        // St has no radio button, so a set of toggles kept mutually exclusive by
        // hand is the closest thing; the labels and the offered set both come
        // from the C side, which knows what this build can construct.
        for (const backend of this._offered) {
            const button = new CheckBox.CheckBox(GPasteDaemon.Prompt.storage_label(backend));

            button.checked = backend === this._current;
            button.connect('clicked', () => this._selectBackend(backend));
            this._backendButtons.set(backend, button);
            backends.add_child(button);
        }

        content.add_child(backends);

        this._import = new CheckBox.CheckBox(_('Import existing data'));
        this._cleanup = new CheckBox.CheckBox(_('Delete old data afterwards'));
        this._import.connect('clicked', () => this._updateState());
        this._cleanup.connect('clicked', () => this._updateState());
        content.add_child(this._import);
        content.add_child(this._cleanup);

        this._warning = new St.Label({
            style_class: 'prompt-dialog-error-label',
            text: _('The old data will be deleted without being imported first'),
        });
        this._warning.clutter_text.ellipsize = Pango.EllipsizeMode.NONE;
        this._warning.clutter_text.line_wrap = true;
        content.add_child(this._warning);

        this.contentLayout.add_child(content);

        this.addButton({
            label: _('Apply'),
            action: () => this._onApply(),
            default: true,
        });
        this.addButton({
            label: _('Cancel'),
            action: () => this.close(),
            key: Clutter.KEY_Escape,
        });

        this._updateState();

        // See the passphrase dialog: exactly one answer, whichever way we go.
        this.connect('closed', () => this._dismiss());
        this.connect('destroy', () => this._dismiss());
    }

    // Dismissing is an answer of its own: the storage layer takes it as "no
    // choice made", leaves the revision alone and asks again on the next start.
    _dismiss() {
        if (this._answered)
            return;

        this._answered = true;
        this._request.dismiss();
    }

    _selectBackend(backend) {
        this._chosen = backend;

        for (const [kind, button] of this._backendButtons)
            button.checked = kind === backend;

        this._updateState();
    }

    // The same rules the Adwaita dialog applies, and for the same reason they
    // are exported from C: importing only makes sense between two different
    // storing backends, and there is only old data to delete once we actually
    // leave one that stored something.
    _updateState() {
        const canImport = GPasteDaemon.Prompt.can_import(this._current, this._chosen);
        const backendChanges = GPasteDaemon.Prompt.backend_changes(this._current, this._chosen);

        this._setSensitive(this._import, canImport);
        this._setSensitive(this._cleanup, backendChanges);

        // Deleting the old data without importing it first throws it away.
        this._warning.visible = this._cleanup.checked && !this._import.checked;
    }

    _setSensitive(check, sensitive) {
        if (!sensitive)
            check.checked = false;

        check.reactive = check.can_focus = sensitive;
        check.opacity = sensitive ? 255 : 128;
    }

    _onApply() {
        if (!this._answered) {
            this._answered = true;
            this._request.reply_migration(this._chosen, this._import.checked, this._cleanup.checked);
        }

        this.close();
    }
});

// ModalDialog.open() fails when it cannot take the modal grab (another system
// modal already holds one). A dialog that never opened also never closes, so
// nothing would ever answer the request — and the storage never settles, which
// leaves _onNameAcquired() waiting forever with the bus name owned and no
// daemon behind it. Treat it as a dismissal instead: the migration is simply
// offered again next start, and an encrypted history stays locked rather than
// being loaded empty.
function present(dialog, request) {
    if (dialog.open())
        return;

    console.error('GPaste: could not open the storage dialog, no modal grab available');
    dialog.destroy();
    request.dismiss();
}

export const GPasteShellPrompt = GObject.registerClass({
    Implements: [GPasteDaemon.Prompt],
}, class GPasteShellPrompt extends GObject.Object {
    constructor() {
        super();

        this._open = new Set();
        this._shutDown = false;
    }

    _show(dialog, request) {
        // Track it so shutdown() can take it away again. Closing is what
        // answers the request, so the entry is dropped from there.
        this._open.add(dialog);
        dialog.connect('destroy', () => this._open.delete(dialog));

        present(dialog, request);
    }

    // The extension is going away, so nothing is left to act on an answer: the
    // runner has stood down and the settings it shared are about to be dropped.
    // Close whatever is still up, which dismisses the request behind it and
    // unblocks the storage settle rather than leaving it pending for good with
    // an ownerless dialog on screen.
    shutdown() {
        // Set first: dismissing a prompt can make the storage layer ask the
        // next question rather than give up — backing out of the passphrase
        // for an encrypted destination sends it straight back to the migration
        // prompt — and answering that with a fresh dialog would put a modal
        // window on screen for an extension that is already gone, still able
        // to rewrite the history on disk.
        this._shutDown = true;

        for (const dialog of [...this._open])
            dialog.close();
    }

    vfunc_passphrase(request) {
        if (this._shutDown)
            request.dismiss();
        else
            this._show(new PassphraseDialog(request), request);
    }

    vfunc_migration(request) {
        if (this._shutDown)
            request.dismiss();
        else
            this._show(new MigrationDialog(request), request);
    }
});
