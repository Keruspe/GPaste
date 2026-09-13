// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';
import {Ornament, PopupBaseMenuItem, PopupMenuItem, PopupSubMenuMenuItem} from 'resource:///org/gnome/shell/ui/popupMenu.js';

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import GPaste from 'gi://GPaste?version=3';
import St from 'gi://St';

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

const GPasteHistoryRow = GObject.registerClass({
    // Activating a menu item closes the menu: PopupMenuBase answers 'activate'
    // with itemActivated(). The menu is meant to stay open on the history just
    // switched to -- that is the whole of what changed, and it is the list below
    // that shows it -- so the row says it its own way, as the placeholder row
    // does for 'retry', and the switcher listens for that instead.
    Signals: {'switch': {param_types: [GObject.TYPE_STRING]}},
}, class GPasteHistoryRow extends PopupMenuItem {
    constructor(history, size) {
        super(history);

        this._history = history;

        // The count at the end of the row, the name taking the room left over.
        this.label.set_x_expand(true);
        this._size = new St.Label({
            x_align: Clutter.ActorAlign.END,
            y_align: Clutter.ActorAlign.CENTER,
        });
        this.add_child(this._size);
        this.setSize(size);
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

export const GPasteHistorySwitcher = GObject.registerClass(
class GPasteHistorySwitcher extends PopupSubMenuMenuItem {
    constructor(client) {
        // The row is the name of the history in use; expanding it is what offers
        // the others. No icon: the name is the whole of what it has to say.
        super('', false);

        this.menu.actor.overlay_scrollbars = true;

        this._client = client;
        // The listing filling the submenu, cancelled and replaced by every pass
        // that starts one -- the chooser opening, a history appearing or going away.
        // Only the latest answer is worth drawing, and an overtaken one is worth
        // stopping rather than merely dropping.
        this._listing = null;
        // The same for the count of the history in use, asked on its own.
        this._sizing = null;
        this._rows = [];
        this._current = null;

        // Last in the submenu, and not one of the rows: it is how a history the
        // list does not hold comes into being.
        this._newItem = new GPasteNewHistoryItem();
        this._newItem.connect('switch', (item, history) => this._switch(history));
        this.menu.addMenuItem(this._newItem);

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

        // The reply has nowhere to land once the row is gone, and listing for it
        // is work the daemon is doing for a menu that has been torn down.
        this.connect('destroy', () => {
            this._listing?.cancel();
            this._sizing?.cancel();
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

        // Every row is rebuilt, and a destroyed actor takes the key focus with
        // it, stranding the keyboard on nothing: it goes back to the row for the
        // same history, or to this one if that history is gone.
        const focus = global.stage.get_key_focus();
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

        if (focused !== undefined)
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
        this._client.switch_history(history, null, null);
        this._newItem.reset();

        // Folding hides whatever in the chooser holds the key focus, and Clutter
        // drops the focus of a hidden actor: it goes to this row, which names
        // the history just asked for -- what the shell's own Left key does when
        // it closes a submenu.
        const focus = global.stage.get_key_focus();

        if (focus !== null && this.menu.actor.contains(focus))
            this.grab_key_focus();

        this.setSubmenuShown(false);
    }

    // Fold the chooser away and drop what was half typed into it: the menu is
    // closing, and it reopens on the history in use rather than on the detour
    // the chooser is.
    collapse() {
        this._newItem.reset();
        this._fold();
    }

    // The submenu is a child of the menu in its own right rather than of this
    // row, so hiding the row leaves it standing: an expanded switcher hidden
    // with the daemon gone would leave its list on screen with nothing above it.
    // Only the fold, and no reset: what the user typed is not this row's to
    // discard for a daemon that went away, and a hide can also be a teardown --
    // where the entry is on its way out and setting text on it reaches an actor
    // that is being destroyed.
    vfunc_hide() {
        this._listing?.cancel();
        this._sizing?.cancel();
        this._fold();
        super.vfunc_hide();
    }

    _fold() {
        this.menu.close({animate: false});
    }
});
