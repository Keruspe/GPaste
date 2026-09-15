// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {gettext as _, pgettext} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Dialog from 'resource:///org/gnome/shell/ui/dialog.js';
import * as ModalDialog from 'resource:///org/gnome/shell/ui/modalDialog.js';
import {Ornament, PopupBaseMenuItem, PopupMenuItem, PopupSubMenuMenuItem} from 'resource:///org/gnome/shell/ui/popupMenu.js';

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import GPaste from 'gi://GPaste?version=3';
import St from 'gi://St';

import {GPasteDeleteButton} from './deleteButton.js';
import {awaitReply, replaceCancellable} from './dependencies.js';

/**
 * Makes an entry of the chooser act on the name typed into it on Enter, or on a
 * click on the mark at its end, which is there to say that the name waits to
 * be confirmed -- nothing about a bare entry says Enter is what does it. The
 * mark is the graphical tool's suffix button and the apply button of a
 * libadwaita entry row, drawn the way the shell's search entry draws its clear
 * icon; like that icon, it is only there while clicking it would do something,
 * so a name that would be held back shows no mark to click in vain.
 *
 * A panel menu has nowhere to report a refusal, so a name that would be refused
 * is not sent -- asked of the very rule the daemon applies -- and the entry
 * keeps it to be amended.
 *
 * @param {St.Entry} entry - the entry
 * @param {string} iconName - the mark, which names what confirming does
 * @param {Function} acceptable - whether a trimmed name would be acted on
 * @param {Function} confirm - acts on the trimmed name
 * @returns {Function} shows or hides the mark again, for when what makes a
 *   name acceptable changes under text that did not
 */
function addConfirmMark(entry, iconName, acceptable, confirm) {
    const mark = new St.Icon({
        style_class: 'search-entry-icon',
        icon_name: iconName,
    });
    const name = () => entry.get_text().trim();
    const onConfirm = () => {
        if (acceptable(name()))
            confirm(name());
    };
    const update = () => entry.set_secondary_icon(acceptable(name()) ? mark : null);

    entry.clutter_text.connect('text-changed', update);
    entry.clutter_text.connect('activate', onConfirm);
    entry.connect('secondary-icon-clicked', onConfirm);

    return update;
}

/**
 * The callback for a call nothing waits on: the menu has closed on it, or never
 * needed its answer, so a refusal -- a daemon busy handing its store over, say --
 * has nowhere to go but the log, and would otherwise be dropped without a trace.
 *
 * @param {string} finish - the name of the call's _finish method
 * @returns {Function} the callback to pass the call
 */
function logFailure(finish) {
    return (client, result) => {
        try {
            client[finish](result);
        } catch (e) {
            console.error(e);
        }
    };
}

// Asks before a history goes or loses what it holds: unlike an item, that is
// everything in it at once, which is also why the graphical tool asks. The words
// are that tool's own, so they arrive already translated.
const GPasteHistoryConfirmationDialog = GObject.registerClass(
class GPasteHistoryConfirmationDialog extends ModalDialog.ModalDialog {
    constructor(title, description, label, action) {
        super();

        this.contentLayout.add_child(new Dialog.MessageDialogContent({title, description}));

        // Cancel is what Escape answers and where the focus starts: a key is as
        // likely as a click to have asked -- Delete on a row, Enter on one of a
        // history's actions -- and Enter pressed once more should not be all it
        // takes to lose a history or what it holds.
        this.addButton({
            label: _('Cancel'),
            action: () => this.close(),
            key: Clutter.KEY_Escape,
            default: true,
        });
        this.addButton({
            label,
            action: () => {
                action();
                this.close();
            },
        });
    }
});

/**
 * The bin on a row naming a history, shown on the row being looked at: the one
 * under the pointer (`hover`) and the one the keyboard is on (`active`), the
 * pointer being kept from setting `active` on these rows -- see
 * GPasteHistoryRow and GPasteHistorySwitcher's `active`.
 *
 * Not focusable, where an item row's is: tabbing into it would clear `active` on
 * the row and hide it under the keyboard, which GPasteItem answers with a watch
 * on the stage's focus. Every row that wears it answers the Delete key with what
 * the button does, so there is nothing the button would add there to be worth
 * that.
 *
 * @param {PopupBaseMenuItem} row - the row the button sits on
 * @param {Function} onClicked - what deleting from that row does
 * @returns {GPasteDeleteButton} the button, for the row to place
 */
function newHistoryDeleteButton(row, onClicked) {
    const button = new GPasteDeleteButton();

    button.can_focus = false;
    button.connect('clicked', onClicked);
    const sync = () => {
        button.visible = row.hover || row.active;
    };

    row.connect('notify::hover', sync);
    row.connect('notify::active', sync);
    sync();

    return button;
}

class GPasteHistoryRow extends PopupMenuItem {
    // Registered here rather than through GObject.registerClass (class ...) for
    // the binding pool, as GPasteItem is.
    static {
        GObject.registerClass({
            // Activating a menu item closes the menu: PopupMenuBase answers
            // 'activate' with itemActivated(). The menu is meant to stay open on
            // the history just switched to -- that is the whole of what changed,
            // and it is the list below that shows it -- so the row says it its
            // own way, as the placeholder row does for 'retry', and the switcher
            // listens for that instead. 'delete' is the row asking for the same
            // thing of its button and of its Delete key, and 'actions' for what
            // else can be done to its history, saying whether the keyboard
            // asked (the menu key) or the pointer did (a right click).
            Signals: {
                'switch': {param_types: [GObject.TYPE_STRING]},
                'delete': {param_types: [GObject.TYPE_STRING]},
                'actions': {param_types: [GObject.TYPE_STRING, GObject.TYPE_BOOLEAN]},
            },
        }, this);

        const bindingPool = this.get_binding_pool();
        const deleteHistory = obj => {
            obj.emit('delete', obj._history);
            return Clutter.EVENT_STOP;
        };

        bindingPool.install_closure('delete', Clutter.KEY_BackSpace, 0, deleteHistory);
        bindingPool.install_closure('delete', Clutter.KEY_Delete, 0, deleteHistory);
    }

    constructor(history, size) {
        // hover: false keeps the pointer from taking the key focus, as it does
        // on an item row (Fix #435): crossing this row would take the keyboard
        // from an entry being typed into, and BackSpace there would then delete
        // this history.
        super(history, {hover: false});

        this._history = history;

        // The name takes the room left over, and so gives up what the button
        // takes when it shows.
        this.label.set_x_expand(true);

        // Ahead of the count rather than after it: shown and hidden with the
        // hover, a button at the end of the row would push the count left and
        // back on every row the pointer crosses.
        this.add_child(newHistoryDeleteButton(this, () => this.emit('delete', this._history)));

        // The count at the end of the row.
        this._size = new St.Label({
            x_align: Clutter.ActorAlign.END,
            y_align: Clutter.ActorAlign.CENTER,
        });
        this.add_child(this._size);
        this.setSize(size);

        // Recognised on press, as the shell's own right clicks are. A gesture
        // that recognises cancels the others on the same points, so the row's
        // own click gesture -- which takes any button -- never gets to the
        // release it would switch on.
        const rightClick = new Clutter.ClickGesture({
            required_button: Clutter.BUTTON_SECONDARY,
            recognize_on_press: true,
        });
        rightClick.connect('recognize', () => this.emit('actions', this._history, false));
        this.add_action(rightClick);

        // What St emits for the menu key and Shift+F10.
        this.connect('popup-menu', () => this.emit('actions', this._history, true));
    }

    get history() {
        return this._history;
    }

    setSize(size) {
        this._size.text = `${size}`;
    }

    // One of a set of mutually exclusive choices is what this is, so it is
    // marked the way the shell marks one: the dot every radio-like menu item
    // wears, rather than a decoration of our own.
    setCurrent(current) {
        this.setOrnament(current ? Ornament.DOT : Ornament.NO_DOT);
    }

    activate(_event) {
        this.emit('switch', this._history);
    }
}

// What can be done to a history besides switching to it, shown under its row on
// a right click. Delete is there too, although the row has a button and a key
// for it: this is where the rest of a history's actions are looked for. Inline
// rather than a menu of its own: a popup menu opened from inside an open one
// would need a grab of its own, and this keeps the chooser where it is, much as
// the entry below it does.
const GPasteHistoryActionsItem = GObject.registerClass({
    Signals: {
        'empty': {param_types: [GObject.TYPE_STRING]},
        'backup': {param_types: [GObject.TYPE_STRING, GObject.TYPE_STRING]},
        'delete': {param_types: [GObject.TYPE_STRING]},
    },
}, class GPasteHistoryActionsItem extends PopupBaseMenuItem {
    // @isTaken says whether a history of that name is listed already.
    constructor(isTaken) {
        // Not activatable, for the reason the entry's row is not: what acts is
        // a button or the entry, and a row that activated would close the menu.
        super({
            activate: false,
            reactive: true,
            hover: false,
            can_focus: false,
        });

        this._history = null;

        this._buttons = new St.BoxLayout({
            x_expand: true,
            style: 'spacing: 6px;',
        });
        this._emptyButton = this._addButton(pgettext('verb', 'Empty'), () => this.emit('empty', this._history));
        this._backupButton = this._addButton(_('Back Up'), () => this._askBackupName());
        this._addButton(_('Delete'), () => this.emit('delete', this._history));
        this.add_child(this._buttons);

        // The name of the copy, asked for in place of the buttons, as the
        // graphical tool asks for it -- prefilled with the name it offers.
        //
        // BackupHistory refuses a name already taken, and the entry would fold
        // away as if the copy had been made: a listed name is held back too. The
        // list can be a listing behind; a refusal that slips through that way
        // is logged.
        this._entry = new St.Entry({
            style_class: 'search-entry',
            x_expand: true,
            track_hover: true,
            reactive: true,
            can_focus: true,
        });
        this._updateMark = addConfirmMark(this._entry, 'object-select-symbolic',
            name => GPaste.util_history_name_is_valid(name) && !isTaken(name),
            name => this.emit('backup', this._history, name));
        this.add_child(this._entry);

        this.reset();
    }

    _addButton(label, action) {
        const button = new St.Button({
            style_class: 'button',
            label,
            x_expand: true,
            can_focus: true,
        });

        button.connect('clicked', action);
        this._buttons.add_child(button);

        return button;
    }

    get history() {
        return this._history;
    }

    // Back to the buttons, for @history. Whatever was typed as the name of a
    // copy was the name of a copy of whatever this showed before.
    reset(history = null) {
        this._history = history;
        this._entry.text = '';
        this._entry.hide();
        this._buttons.show();
    }

    focus() {
        this._emptyButton.grab_key_focus();
    }

    _askBackupName() {
        this._buttons.hide();
        this._entry.show();
        this._entry.text = `${this._history}_backup`;
        this._entry.clutter_text.grab_key_focus();
        // Selected, so typing replaces the offer rather than adding to it.
        this._entry.clutter_text.set_selection(0, -1);
    }

    // The list the name is checked against has changed.
    revalidate() {
        this._updateMark();
    }

    // Escape on the name, as Cancel answers the graphical tool's dialog: back
    // to the buttons, and to the one the keyboard came from, since hiding the
    // entry drops the key focus with it. Only while the keyboard is in the
    // entry; says whether that was the case.
    cancelBackupName() {
        const focus = global.stage.get_key_focus();

        if (!this._entry.visible || focus === null || !this._entry.contains(focus))
            return false;

        this.reset(this._history);
        this._backupButton.grab_key_focus();
        return true;
    }
});

const GPasteNewHistoryItem = GObject.registerClass({
    Signals: {'switch': {param_types: [GObject.TYPE_STRING]}},
}, class GPasteNewHistoryItem extends PopupBaseMenuItem {
    constructor() {
        // Not activatable: what acts is the entry's own Enter, and a row that
        // activated would close the menu on the history it has just asked for.
        super({
            activate: false,
            reactive: true,
            hover: false,
            can_focus: false,
        });

        // That a name nobody has used before makes a history rather than failing
        // to find one is not something a user can be expected to guess, and a
        // hint is the only place this row has to say it. As wide as the row, as
        // the search entry is as wide as the menu: sized to its hint, it would
        // stop short of the rows above it.
        this._entry = new St.Entry({
            style_class: 'search-entry',
            hint_text: _('Switch to or create a history…'),
            x_expand: true,
            track_hover: true,
            reactive: true,
            can_focus: true,
        });
        this._entry.set_primary_icon(new St.Icon({
            style_class: 'search-entry-icon',
            icon_name: 'list-add-symbolic',
        }));
        // The graphical tool's own mark for switching or creating.
        addConfirmMark(this._entry, 'go-jump-symbolic',
            name => GPaste.util_history_name_is_valid(name),
            name => this.emit('switch', name));
        this.add_child(this._entry);
    }

    reset() {
        this._entry.text = '';
    }
});

export class GPasteHistorySwitcher extends PopupSubMenuMenuItem {
    // Registered here rather than through GObject.registerClass (class ...) for
    // the binding pool, as GPasteHistoryRow is. The keys a history row deletes
    // its history with delete the one this row names: the row wears the same
    // bin, shown under the keyboard as much as under the pointer, and a button
    // the keyboard is shown but cannot press is not one to leave it with.
    static {
        GObject.registerClass(this);

        const bindingPool = this.get_binding_pool();
        const deleteCurrent = obj => {
            obj._deleteCurrent();
            return Clutter.EVENT_STOP;
        };

        bindingPool.install_closure('delete', Clutter.KEY_BackSpace, 0, deleteCurrent);
        bindingPool.install_closure('delete', Clutter.KEY_Delete, 0, deleteCurrent);
    }

    // `hover: false`, for the reason GPasteHistoryRow passes it, but
    // PopupSubMenuMenuItem takes no parameters to pass it with: the shell's
    // binding of `hover` to `active` is there, so it is `active` that refuses
    // what does not match the key focus. That leaves `active` meaning what it
    // means on the history rows -- where the keyboard is -- rather than turning
    // on under the pointer, which would take the key focus or, with the grab
    // held back, make the menu deactivate the row that does have it. Clutter
    // records the focus before it emits key-focus-in and key-focus-out, so the
    // row's own handlers always get through.
    get active() {
        return super.active;
    }

    set active(active) {
        if (active === this.has_key_focus())
            super.active = active;
    }

    // @topMenu is the menu this row sits in, for the key it has to see first.
    constructor(client, settings, topMenu) {
        // The row is the name of the history in use; expanding it is what offers
        // the others. No icon: the name is the whole of what it has to say.
        super('', false);

        this.menu.actor.overlay_scrollbars = true;

        this._client = client;
        this._settings = settings;

        // The history in use is deleted from the row naming it too, not only
        // from its row in the chooser, which takes expanding to reach. Ahead of
        // the arrow, which stays where the shell puts it on every submenu row:
        // right after the expander pushing that arrow to the end, found by the
        // style class the theme knows it by rather than through a field private
        // to the shell. Were it gone, the bin would still end the row instead
        // of landing ahead of the name and moving it on every hover.
        const expander = this.get_children().find(child => child.has_style_class_name('popup-menu-item-expander'));

        this.insert_child_above(newHistoryDeleteButton(this, () => this._deleteCurrent()), expander ?? null);

        // The listing filling the submenu, cancelled and replaced by every pass
        // that starts one -- the chooser opening, a history appearing or going
        // away. Only the latest answer is worth drawing (see
        // replaceCancellable ()).
        this._listing = null;
        // The same for the count of the history in use, asked on its own.
        this._sizing = null;
        this._rows = [];
        this._current = null;
        // The confirmation on screen, if any, for a teardown to take away.
        this._dialog = null;

        // Last in the submenu, and not one of the rows: it is how a history the
        // list does not hold comes into being.
        this._newItem = new GPasteNewHistoryItem();
        this._newItem.connect('switch', (item, history) => this._switch(history));
        this.menu.addMenuItem(this._newItem);

        // One for whichever row was last right clicked, moved to sit under it
        // and hidden the rest of the time, rather than one built per click: it
        // is its own buttons that set off its going away, and an actor is not
        // to be destroyed from inside its own child's handler. Built after the
        // entry and moved ahead of it, so that once the rows are cleared it is
        // right before the entry, and rows added at their own count still land
        // ahead of both.
        this._actions = new GPasteHistoryActionsItem(name => this._rows.some(row => row.history === name));
        this._actions.connect('empty', (item, history) => this._empty(history));
        this._actions.connect('backup', (item, history, name) => this._backup(history, name));
        // The row's own path, dialog and all; closing the menu for the dialog
        // is what folds the actions away.
        this._actions.connect('delete', (item, history) => this._confirmDelete(history));
        this.menu.addMenuItem(this._actions, 0);
        this._actions.hide();

        // Escape on the name of a backup goes back to the buttons rather than
        // closing the whole menu. The menu manager closes it from a controller
        // on the menu's actor -- the actor its grab is on, so the top of every
        // key's path -- in the capture phase, which is ahead of anything the
        // entry could install. Actions on one actor run in the order they were
        // added, and the manager adds its controller afresh on every open, so
        // one added here, for good, is ahead of it from the next open on.
        this._escapeController = new Clutter.KeyController();
        this._escapeController.connect('key-press', controller => {
            const [, symbol] = controller.get_key();

            if (symbol === Clutter.KEY_Escape && this._actions.cancelBackupName())
                return Clutter.EVENT_STOP;

            return Clutter.EVENT_PROPAGATE;
        });
        this._topMenu = topMenu;
        this._topMenu.actor.add_action_full('gpaste-backup-name-escape',
            Clutter.EventPhase.CAPTURE, this._escapeController);

        this._client.connectObject(
            // A backup or a delete changes the set of histories without changing
            // which one is current, which no notify::history can express. The
            // daemon follows every HistoryDeleted with it, so that one needs no
            // listing of its own. An empty changes no set but does change a
            // count, and says so only through history-emptied; emptying a history
            // that did not exist yet sends both, the first listing then given up
            // on for the second.
            'histories-changed', () => this._refreshIfOpen(),
            'history-emptied', () => this._refreshIfOpen(),
            'notify::history', this._onHistoryChanged.bind(this),
            'update', this._onUpdate.bind(this),
            this);

        // The reply has nowhere to land once the row is gone. A question still
        // up is about that menu too: the dialog lives in the shell's modal group
        // rather than under this row, so it would stay on screen, grab and all,
        // still able to act for an extension that is gone.
        this.connect('destroy', () => {
            this._listing?.cancel();
            this._sizing?.cancel();
            this._dialog?.close();
            this._topMenu.actor.remove_action(this._escapeController);
        });

        this._onHistoryChanged();
    }

    // The property is cleared, not merely changed, when the daemon leaves the
    // bus: there is no name to show then, and no row to mark.
    _onHistoryChanged() {
        this._current = this._client.get_history_name();
        this.label.text = this._current ?? '';
        this._markCurrent();
    }

    // ListHistories parses inactive file histories on the daemon's main thread.
    // Pay for it only while the chooser is being used, including reopening to
    // pick up changes made on disk outside this daemon.
    setSubmenuShown(open) {
        super.setSubmenuShown(open);
        if (open) {
            // Shell chooses scrolling from the rows present at open(), before
            // our async listing arrives. Automatic policy follows later growth
            // too; overlay scrollbars avoid reserving a column for a short list.
            this.menu.actor.vscrollbar_policy = St.PolicyType.AUTOMATIC;
            this._refreshIfOpen();
        }
    }

    _refreshIfOpen() {
        if (this.menu.isOpen && this._client.get_name_owner())
            this.refresh().catch(console.error);
    }

    // The count of the history in use changes with every copy, which no listing
    // is asked for: re-enumerating every inactive history on each would cost
    // what GetHistorySize, the length of the history already loaded, does not.
    // Asked for here rather than borrowed from the list below the chooser, which
    // asks for none while it is filtered and whose own asking a filter pass can
    // cancel. Only an update that can change the count asks: a single item
    // replaced in place leaves it as it was.
    _onUpdate(client, action, target) {
        if (target === GPaste.UpdateTarget.ALL || action === GPaste.UpdateAction.REMOVE)
            this._sizeCurrent().catch(console.error);
    }

    // Only while there is a chooser to show it, and a daemon to ask. A size and
    // a listing land in the order they were asked, the daemon answering both
    // from its main loop, so whichever landed last is the newer count.
    async _sizeCurrent() {
        if (!this.menu.isOpen || !this._client.get_name_owner())
            return;

        const cancellable = this._sizing = replaceCancellable(this._sizing);
        let wanted, size;

        try {
            [wanted, size] = await awaitReply(cancellable, this._client.get_history_size(cancellable));
        } catch (e) {
            console.error(e);
            return;
        }

        if (wanted)
            this._rows.find(row => row.history === this._current)?.setSize(size);
    }

    _markCurrent() {
        this._rows.forEach(row => row.setCurrent(row.history === this._current));
    }

    // One call answers every history and how much each holds, so the whole
    // submenu costs a single round trip rather than one per row.
    //
    // Only ever called with a daemon there: an ordinary method call is also what
    // bus-activates one, and a menu opening must not put back the daemon the
    // user has just stopped.
    async refresh() {
        const cancellable = this._listing = replaceCancellable(this._listing);
        let wanted, histories;

        try {
            [wanted, histories] = await awaitReply(cancellable, this._client.list_histories(cancellable));
        } catch (e) {
            console.error(e);
            return;
        }

        if (!wanted)
            return;

        // The actions actor owns the backup draft and focus independently of
        // its row. Keep it while its source history still exists.
        const actionsHistory = this._actions.visible ? this._actions.history : null;
        const focus = global.stage.get_key_focus();
        const actionsFocused = focus !== null && this._actions.contains(focus);

        if (actionsHistory !== null && actionsHistory !== GPaste.DEFAULT_HISTORY &&
            !histories.some(history => history.get_name() === actionsHistory))
            this._hideActions();

        this.menu.moveMenuItem(this._actions, this._rows.length);

        // Every row is rebuilt, and a destroyed actor takes the key focus with
        // it, stranding the keyboard on nothing: it goes back to the row for the
        // same history, or to this one if that history is gone.
        const focused = this._rows.find(row => focus !== null && row.contains(focus))?.history;

        this._clearRows();

        // The default history is always drawn, listed or not: it is where a
        // switch away from a deleted history lands, and it exists whether or not
        // anything has been stored in it yet. Zero until the listing names it,
        // which is also what it holds when there is no store for it.
        this._addRow(GPaste.DEFAULT_HISTORY, 0);

        for (const history of histories)
            this._addRow(history.get_name(), history.get_size());

        this._markCurrent();

        // A listing names the histories there is a store for, and the one in use
        // may have none: the `none` storage lists nothing at all, and the
        // default history is drawn whether or not it is listed. Its row would
        // otherwise say 0 however much it holds.
        if (this._current !== null && !histories.some(history => history.get_name() === this._current))
            this._sizeCurrent().catch(console.error);

        if (this._actions.visible) {
            const index = this._rows.findIndex(row => row.history === actionsHistory);

            this.menu.moveMenuItem(this._actions, index + 1);
            // A name the rebuilt list takes, or gives back, changes what the
            // mark offers while the name stays as it was typed.
            this._actions.revalidate();
        }

        if (actionsFocused && this._actions.visible)
            focus.grab_key_focus();
        else if (actionsFocused)
            this.grab_key_focus();
        else if (focused !== undefined)
            (this._rows.find(row => row.history === focused) ?? this).grab_key_focus();
    }

    _addRow(history, size) {
        const known = this._rows.find(row => row.history === history);

        // The default history was drawn before the listing was walked, so the
        // listing naming it is what says how much it holds.
        if (known) {
            known.setSize(size);
            return;
        }

        const row = new GPasteHistoryRow(history, size);

        row.connect('switch', (item, name) => this._switch(name));
        row.connect('delete', (item, name) => this._confirmDelete(name));
        row.connect('actions', (item, name, fromKeyboard) => this._toggleActions(item, fromKeyboard));
        // Ahead of the entry, which stays last however many rows there are.
        this.menu.addMenuItem(row, this._rows.length);
        this._rows.push(row);
    }

    _clearRows() {
        this._rows.forEach(row => row.destroy());
        this._rows = [];
    }

    // Switching to a name no history goes by creates it: that is how a new
    // history comes into being, there being no call of its own for it, and it is
    // what makes one entry enough for both.
    //
    // The menu stays open on the history just asked for. What changes is the
    // list below, which the daemon announces with an update of its own once the
    // new history has loaded.
    _switch(history) {
        this._client.switch_history(history, null, logFailure('switch_history_finish'));
        this._newItem.reset();
        this._hideActions();

        // Folding hides whatever in the chooser holds the key focus, and Clutter
        // drops the focus of a hidden actor: it goes to this row, which names
        // the history just asked for -- what the shell's own Left key does when
        // it closes a submenu.
        const focus = global.stage.get_key_focus();

        if (focus !== null && this.menu.actor.contains(focus))
            this.grab_key_focus();

        this.setSubmenuShown(false);
    }

    // The same row again folds the actions away; another row takes them.
    // Focused when the keyboard asked, which has nowhere else to reach them
    // from; a click leaves the focus with the pointer.
    _toggleActions(row, fromKeyboard) {
        if (this._actions.visible && this._actions.history === row.history) {
            this._hideActions();
            return;
        }

        this._actions.reset(row.history);
        // Counted without the actions item itself, so the row's own index plus
        // one is right under it.
        this.menu.moveMenuItem(this._actions, this._rows.indexOf(row) + 1);
        this._actions.show();

        if (fromKeyboard)
            this._actions.focus();
    }

    // A focused actor that is hidden is one Clutter drops the key focus for,
    // which would strand a keyboard that has just used one of the buttons: it
    // goes back to the row the actions were for. Not when the menu is closing,
    // where the focus is the menu's own to hand back.
    _hideActions(refocus = true) {
        const focus = global.stage.get_key_focus();

        if (refocus && focus !== null && this._actions.contains(focus))
            this._rows.find(row => row.history === this._actions.history)?.grab_key_focus();

        this._actions.hide();
        this._actions.reset();
    }

    // Asked the way a delete is, where the setting says to ask, rather than in
    // the graphical tool's window: starting that tool for a question is a
    // detour out of the shell for what the shell can ask itself. Without the
    // setting nothing opens, and the menu stays on the history just emptied.
    _empty(history) {
        this._hideActions();

        if (!this._settings.get_empty_history_confirmation()) {
            this._client.empty_history(history, null, logFailure('empty_history_finish'));
            return;
        }

        this._confirm(
            // Translators: %s is the name of the history being emptied.
            _('Empty “%s”?').format(history),
            _('Every item it holds is deleted for good.'),
            pgettext('verb', 'Empty'),
            () => this._client.empty_history(history, null, logFailure('empty_history_finish')));
    }

    // The copy is a new history, which the daemon announces with
    // histories-changed: that is what lists it. The entry has already held back
    // a name the list holds.
    _backup(history, name) {
        this._hideActions();
        this._client.backup_history(history, name, null, logFailure('backup_history_finish'));
    }

    // Nothing is redrawn here once the answer is yes: the daemon announces the
    // deletion, which is what refreshes the list, and deleting the current
    // history switches to the default one, which is what renames the row.
    _confirmDelete(history) {
        this._confirm(
            // Translators: %s is the name of the history being deleted.
            _('Delete “%s”?').format(history),
            _('The history and everything in it are deleted for good.'),
            _('Delete'),
            () => this._client.delete_history(history, null, logFailure('delete_history_finish')));
    }

    // A modal dialog cannot take its grab from under an open menu, so the menu
    // goes first -- as it would have for any item that opens a dialog.
    _confirm(title, description, label, action) {
        this._getTopMenu().close();

        const dialog = this._dialog = new GPasteHistoryConfirmationDialog(title, description, label, () => {
            // A closing animation can leave its button reachable after name loss.
            if (this._client.get_name_owner())
                action();
        });

        // Closing is what destroys it, which is when it stops being the
        // teardown's to close.
        dialog.connectObject('destroy', () => {
            if (this._dialog === dialog)
                this._dialog = null;
        }, this);

        dialog.open();
    }

    // No name, no history to delete: the property is cached off the daemon, and
    // reads back null while it is away.
    _deleteCurrent() {
        if (this._current)
            this._confirmDelete(this._current);
    }

    // Fold the chooser away and drop what was half typed into it: the menu is
    // closing, and it reopens on the history in use rather than on the detour
    // the chooser is.
    collapse() {
        this._newItem.reset();
        this._hideActions(false);
        this._fold();
    }

    // The submenu is a child of the menu in its own right rather than of this
    // row, so hiding the row leaves it standing: an expanded switcher hidden
    // with the daemon gone would leave its list on screen with nothing above it.
    // Only the fold and hiding the actions, and no reset: what the user typed
    // is not this row's to discard for a daemon that went away, and a hide can
    // also be a teardown -- where the entries are on their way out and setting
    // text on one reaches an actor that is being destroyed.
    vfunc_hide() {
        // The modal belongs to the Shell's dialog group, so hiding this row
        // alone cannot revoke a pending destructive action after daemon exit.
        this._dialog?.close();
        this._listing?.cancel();
        this._sizing?.cancel();
        this._fold();
        this._actions.hide();
        super.vfunc_hide();
    }

    _fold() {
        this.menu.close({animate: false});
    }
}
