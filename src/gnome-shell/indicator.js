// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import './dependencies.js';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';

import {ensureActorVisibleInScrollView} from 'resource:///org/gnome/shell/misc/animationUtils.js';
import {Button} from 'resource:///org/gnome/shell/ui/panelMenu.js';
import {PopupMenuSection} from 'resource:///org/gnome/shell/ui/popupMenu.js';

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import GLib from 'gi://GLib';
import St from 'gi://St';
import GPaste from 'gi://GPaste?version=3';

import {addGPasteFooter} from './actions.js';
import {GPasteDummyHistoryItem} from './dummyHistoryItem.js';
import {GPasteItem} from './item.js';
import {GPasteSearchItem} from './searchItem.js';
import {GPasteStateSwitch} from './stateSwitch.js';

export const GPasteIndicator = GObject.registerClass(
class GPasteIndicator extends Button {
    static _CONNECT_RETRIES = 3;
    // The reconnect ladder, in seconds: 1, 2, 4, ... 64, which is a little over
    // two minutes of trying in all. Past it the placeholder row is the way back.
    static _RECONNECT_FIRST_DELAY = 1;
    static _RECONNECT_LAST_DELAY = 64;
    // Rows to load before any have been laid out (and a real row height is
    // known); _maybeLoadMore() then tops the list up to fill the viewport.
    static _DEFAULT_BATCH = 20;

    constructor(extension) {
        super(0.0, 'GPaste');

        this._extension = extension;

        this._statusIcon = new St.BoxLayout({style_class: 'panel-status-menu-box'});
        this._statusIcon.add_child(new St.Icon({
            icon_name: 'edit-paste-symbolic',
            style_class: 'system-status-icon',
        }));
        this.add_child(this._statusIcon);

        this._settings = new GPaste.Settings();
        this._destroyed = false;

        // The rows currently materialised in the scrollable history section.
        // We never hold the whole history: rows are appended in batches as the
        // user scrolls (lazy loading), so St does not have to lay out thousands
        // of actors it cannot recycle.
        this._history = [];
        this._filteredUuids = [];
        this._available = 0;
        this._loading = false;
        this._reloadGeneration = 0;

        // Whether there is a daemon to talk to, and the reconnect ladder we
        // walk while there is none.
        this._connected = false;
        this._reconnectId = 0;
        this._reconnectDelay = 0;
        // Whether the ladder has been walked to its end with nothing found: the
        // placeholder then stops saying "wait" and starts offering a retry.
        this._reconnectSpent = false;
        this._connecting = false;
        // Which probe's reply still counts: the ladder is the latest one's to
        // step, an older one having been overtaken.
        this._probeGeneration = 0;

        this._dummyHistoryItem = new GPasteDummyHistoryItem();
        // Its own signal rather than 'activate', which would close the menu on
        // the row about to report what this did. Emitted only while it is saying
        // there is no daemon, that being the one state where the row is an offer
        // rather than a label.
        this._dummyHistoryItem.connect('retry', this._retry.bind(this));
        this.menu.addMenuItem(this._dummyHistoryItem);

        this._searchItem = new GPasteSearchItem();
        this._searchItem.connect('text-changed', this._reloadCurrent.bind(this));
        this._searchItem.connect('favourites-changed', this._reloadCurrent.bind(this));

        this._settings.connectObject(
            'notify::element-size', this._resetElementSize.bind(this),
            'notify::images-preview', this._resetImagesPreview.bind(this),
            'notify::images-preview-size', this._resetImagesPreview.bind(this),
            this);
        this._resetElementSize();

        // Connected here rather than at the end of _setup (): the settings
        // above are already connected and a reconnect may already be pending
        // by the time _setup () gives up, and a teardown before it finishes
        // has to take those with it too.
        this.connect('destroy', this._onDestroy.bind(this));

        this._setup().catch(console.error);
    }

    async _connect(retries = GPasteIndicator._CONNECT_RETRIES, delay = 1) {
        try {
            // GJS' Gio._promisify cannot replace the static GPaste.Client.new
            // constructor inside gnome-shell (the class-object property assignment
            // does not stick), so promise-wrap the raw async pair by hand.
            return await new Promise((resolve, reject) => {
                GPaste.Client.new((_source, res) => {
                    try {
                        resolve(GPaste.Client.new_finish(res));
                    } catch (e) {
                        reject(e);
                    }
                });
            });
        } catch (e) {
            if (retries <= 0) {
                console.error(`GPaste: ${e.message}`);
                return null;
            }
            await new Promise(resolve => setTimeout(resolve, delay * 1000));
            if (this._destroyed)
                return null;
            return this._connect(retries - 1, delay * 2);
        }
    }

    async _setup() {
        // Marks the window where this._client is not assigned yet. _connect ()
        // retries with a backoff, so that window is seconds long, and a second
        // _setup () started inside it -- the placeholder row is an offer the
        // whole time -- would build a second switch, search row, scroll view and
        // footer into the same menu, orphaning the first of each.
        this._connecting = true;
        try {
            this._client = await this._connect();
        } finally {
            this._connecting = false;
        }

        if (this._destroyed || !this._client) {
            // Out of retries: the placeholder has been saying "Loading…" all
            // this while, and now there is something to report.
            if (!this._destroyed)
                this._dummyHistoryItem.showDisconnected();
            this._client = null;
            return;
        }

        this._switch = new GPasteStateSwitch(this._client);

        // Header, inserted before the dummy placeholder added in the constructor.
        this.menu.addMenuItem(this._switch, 0);
        this.menu.addMenuItem(this._searchItem, 1);

        // The lazily-filled, scrollable history lives in a PopupMenuSection
        // wrapped in an St.ScrollView, between the dummy and the footer.
        this._historySection = new PopupMenuSection();
        this._scrollView = new St.ScrollView({
            hscrollbar_policy: St.PolicyType.NEVER,
            vscrollbar_policy: St.PolicyType.AUTOMATIC,
            overlay_scrollbars: true,
        });
        this._scrollView.child = this._historySection.actor;
        // Fade the history out near the top/bottom edges rather than hard-clipping
        // it against the dummy row and the footer separator.
        this._scrollView.update_fade_effect(new Clutter.Margin({top: 16, bottom: 16}));

        this._footer = addGPasteFooter(this.menu, this._extension, this._client, this._settings);
        this._emptyHistoryItem = this._footer.empty;

        const dummyIndex = this.menu.box.get_children().indexOf(this._dummyHistoryItem);
        this.menu.box.insert_child_at_index(this._scrollView, dummyIndex + 1);

        this._updateScrollHeight();

        // The max-height is derived from the work area of the monitor the
        // indicator lives on; recompute it when the monitor layout changes
        // (resolution, monitor hot-plug, panel moved to another monitor).
        Main.layoutManager.connectObject('monitors-changed', this._updateScrollHeight.bind(this), this);

        // "changed" fires when the content/viewport is resized (fill until the
        // viewport overflows); "notify::value" fires on scroll (load the next
        // batch when the bottom is reached).
        this._scrollView.vadjustment.connectObject(
            'changed', this._maybeLoadMore.bind(this),
            'notify::value', this._maybeLoadMore.bind(this),
            this);

        this._client.connectObject(
            'update', this._update.bind(this),
            'show-history', this._popup.bind(this),
            'tracking', this._toggle.bind(this),
            'notify::history', this._onDaemonStateChanged.bind(this),
            this);

        // The proxy is built whether or not a daemon answered, and it follows
        // the bus name across a re-exec on its own, so this is the first place
        // that can tell one case from the other. Read once the handler above is
        // watching, and not before: a daemon leaving in between would go
        // unannounced, and _onDaemonStateChanged () would then find the very
        // state it was told to expect and stand down for the rest of the
        // session.
        this._connected = this._daemonReady();

        if (!this._connected)
            this._onDaemonGone();

        // A no-op with no daemon there, and deliberately: see _onStateChanged ().
        // _onDaemonAppeared () reports it once one turns up.
        this._onStateChanged(true);

        // The ctrl-index overlay and ctrl+0-9 selection are driven by raw key
        // events. The menu object is a Signals.EventEmitter and never emits
        // 'key-press-event'/'key-release-event'; those fire on its actor, so the
        // handlers have to be connected there.
        this.menu.actor.connect('key-press-event', this._onKeyPressEvent.bind(this));
        this.menu.actor.connect('key-release-event', this._onKeyReleaseEvent.bind(this));

        // Last, being the one step that waits on the daemon: one that drops out
        // while it is in flight rejects the call, and a rejection out of here
        // abandons the rest of this function -- so everything that notices a
        // daemon leaving and offers a way back is connected above it, where an
        // abandoned setup costs the indicator neither its daemon handler nor its
        // ladder for the session.
        if (this._connected)
            await this._reload();
    }

    // @extensionDisabled: whether the extension itself is going away, as opposed
    // to the indicator merely being torn down for a non-user session mode (the
    // lock screen), where the extension — and the in-shell daemon it may host —
    // deliberately stays enabled.
    //
    // Returns whether the extension's state was reported to the daemon, so the
    // caller knows when it still has to report it some other way: we can only do
    // it through a client we may never have got (the daemon was unreachable, or
    // is still being connected to).
    shutdown(extensionDisabled) {
        this._destroyed = true;
        // Only report the extension's own state: "track-extension-state" users
        // ask the daemon to stop tracking when the extension goes away, and the
        // lock screen is not that. Reporting it there would stop recording the
        // clipboard for the whole locked session, which is exactly what the
        // "unlock-dialog" session mode exists to prevent.
        const reported = extensionDisabled && !!this._client && this._connected;

        if (reported)
            this._onStateChanged(false);

        // destroy() fires the 'destroy' signal connected in the constructor,
        // which runs _onDestroy(); don't call it a second time here.
        this.destroy();

        return reported;
    }

    // Which history is in use is the daemon's own property, cached off the
    // proxy, and it reads back null while there is no daemon: whether it has a
    // value is therefore the honest answer to "is there one to ask".
    //
    // The bus name is not that answer. Both daemons own the name *before*
    // building the object that serves it, so the proxy's GetAll on a new owner
    // finds nothing at that path and leaves the cache empty; what fills it is
    // the daemon's own PropertiesChanged a moment later. A reload driven off
    // "g-name-owner" would run in that gap and find the very null it is meant
    // to be past -- which is what a re-exec leaves a client sitting in.
    _daemonReady() {
        return !!this._client?.get_history_name();
    }

    // A daemon appearing or going away is a change of what the whole menu can
    // say, so it is answered here rather than by each path finding out for
    // itself that its call went nowhere. "notify::history" carries both edges:
    // the proxy invalidates every cached property the moment the name loses its
    // owner, and fills them again once there is something to fill them from.
    //
    // Returns whether the state actually moved, which is what _reconcileConnection()
    // asks: a caller that gave up on a daemon still sitting there has been told
    // nothing by this, and has its own repainting to do.
    _onDaemonStateChanged() {
        const connected = this._daemonReady();

        if (connected === this._connected)
            return false;

        if (connected)
            this._onDaemonAppeared();
        else
            this._onDaemonGone();

        return true;
    }

    _onDaemonAppeared() {
        this._connected = true;
        this._cancelReconnect();
        this._reconnectDelay = 0;
        this._reconnectSpent = false;
        // Held back for as long as there was no daemon to tell, this one having
        // come up after the extension did.
        this._onStateChanged(true);
        this._reloadCurrent();
    }

    // Drop everything the menu was showing of a history it can no longer speak
    // for. @_available reaching 0 before the rows go is what the order is for:
    // emptying them moves the scroll adjustment, and its "changed" would have
    // _maybeLoadMore() fill them again from a count that still said there was
    // more to come.
    _forgetHistory() {
        this._available = 0;
        this._filteredUuids = [];
        this._clearRows();
        this._updateVisibility(true);
    }

    _onDaemonGone() {
        this._connected = false;
        // A reply about the history that just went away must not land on the
        // rows the next daemon fills.
        ++this._reloadGeneration;
        this._forgetHistory();

        this._reconnectDelay = 0;
        this._reconnectSpent = false;
        this._scheduleReconnect();
    }

    // The daemon is bus-activatable, so an ordinary method call is what brings
    // it back -- which is exactly what _fetchAvailable ()'s cached-name guard
    // exists to avoid doing by accident, and what this one is for.
    //
    // @activate: whether asking may be what starts a daemon. Only the user gets
    // to say so, by activating the placeholder row: a ladder that activated on
    // its own would put back the daemon they had just stopped, within a second
    // of their stopping it, with no way to suppress it short of turning the
    // extension off. So a scheduled probe waits for the name to have an owner
    // and only then asks -- which is the gap it exists for anyway, a daemon
    // owning the name before it exports anything.
    //
    // The reply is not the answer, and a failure is not one either: a daemon
    // that has just been activated owns the name before it exports anything, so
    // the call that started it is as likely as not to come back "object does not
    // exist". What says it worked is _onDaemonStateChanged () firing.
    async _probeDaemon(activate = false) {
        if (this._destroyed || !this._client || this._connected)
            return;

        if (!activate && !this._client.get_name_owner()) {
            this._scheduleReconnect();
            return;
        }

        // Only the latest probe's reply is worth anything: a retry landing while
        // a scheduled one is still awaiting its call restarts the ladder, and the
        // overtaken probe stepping it on its way out would walk it twice per
        // click -- spending it well before the two minutes it is meant to cover.
        const generation = ++this._probeGeneration;

        try {
            await this._client.get_history_size();
        } catch {
            // Nothing to report: a probe that got nowhere is what the next rung
            // is for, and the last rung leaves the placeholder row to be asked
            // again.
        }

        // A daemon that did come up announced itself, which cancelled what is
        // scheduled here; one that did not leaves the ladder to carry on --
        // unless a newer probe has taken it over, which owns what happens next.
        if (this._destroyed || this._connected || generation !== this._probeGeneration)
            return;

        this._scheduleReconnect();
        // And the row says which of "starting" and "not running" is true now, a
        // retry having left it saying neither.
        this._updateVisibility(true);
    }

    // Back off between tries so a session with no daemon to activate is not
    // polled forever, and stop once the ladder is walked: two minutes is long
    // enough for a re-exec, an upgrade or a manual restart, and past that
    // retrying is the user's call.
    _scheduleReconnect() {
        this._cancelReconnect();

        const delay = this._reconnectDelay
            ? this._reconnectDelay * 2
            : GPasteIndicator._RECONNECT_FIRST_DELAY;

        if (delay > GPasteIndicator._RECONNECT_LAST_DELAY) {
            // Nothing is coming to fix this on its own, so the placeholder stops
            // saying "starting" and becomes the way back -- including for a
            // daemon that owns the bus name and never answered, which is what
            // "Loading…" was waiting on and has now waited out.
            this._reconnectSpent = true;
            this._updateVisibility(true);
            return;
        }

        this._reconnectDelay = delay;
        this._reconnectId = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, delay, () => {
            this._reconnectId = 0;
            this._probeDaemon().catch(console.error);
            return GLib.SOURCE_REMOVE;
        });
        GLib.Source.set_name_by_id(this._reconnectId, '[GPaste] daemon reconnect');
    }

    _cancelReconnect() {
        if (this._reconnectId) {
            GLib.Source.remove(this._reconnectId);
            this._reconnectId = 0;
        }
    }

    // The placeholder row was activated: try again now -- asking for a daemon
    // rather than waiting for one, since this is the user asking -- and start
    // the ladder over from its first rung rather than from where it left off.
    _retry() {
        if (this._destroyed)
            return;

        // A _setup () still inside _connect () owns the rebuild; starting
        // another here is what would duplicate the menu.
        if (this._connecting)
            return;

        this._reconnectDelay = 0;
        this._reconnectSpent = false;
        this._cancelReconnect();

        // The menu stays open on activation, so this is the row the user is
        // still looking at: it says the offer was taken before anything goes out
        // on the bus, and whoever answers paints the outcome over it. It also
        // stops being an offer while that is in flight, a second click having
        // nothing to add to the first.
        this._dummyHistoryItem.showLoading();

        // Running out of connect retries leaves no proxy at all, so there is
        // nothing to probe with, and the row would be an offer that does
        // nothing. _setup () returned before it built anything in that case, so
        // what it takes is simply running it again.
        if (!this._client) {
            this._setup().catch(console.error);
            return;
        }

        this._probeDaemon(true).catch(console.error);
    }

    _onKeyPressEvent(actor, event) {
        if (event.has_control_modifier()) {
            const nb = parseInt(event.get_key_unicode(), 10);
            if (!Number.isNaN(nb) && nb >= 0 && nb <= 9 && nb < this._history.length)
                this._history[nb].activate(event);
        } else {
            this._maybeUpdateIndexVisibility(event, true);
        }
    }

    _onKeyReleaseEvent(actor, event) {
        this._updateIndexVisibility(!this._eventIsControlKey(event) && event.has_control_modifier());
    }

    _maybeUpdateIndexVisibility(event, state) {
        if (this._eventIsControlKey(event))
            this._updateIndexVisibility(state);
    }

    _updateIndexVisibility(state) {
        this._history.slice(0, 10).forEach(i => {
            i.showIndex(state);
        });
    }

    _eventIsControlKey(event) {
        const key = event.get_key_symbol();
        return key === Clutter.KEY_Control_L || key === Clutter.KEY_Control_R;
    }

    _hasSearch() {
        return this._searchItem.text.length > 0;
    }

    // Both filters list rows by uuid rather than by history position, so they
    // share every path that cares about which of the two the list is showing.
    _isFiltered() {
        return this._hasSearch() || this._searchItem.favourites;
    }

    _totalSize() {
        return this._isFiltered() ? this._filteredUuids.length : this._available;
    }

    _resetElementSize() {
        const size = this._settings.get_element_size();
        this._searchItem.resetSize(size / 2 + 3);
        this._history.forEach(i => {
            i.setTextSize(size);
        });
    }

    _resetImagesPreview() {
        const enabled = this._settings.get_images_preview();
        const size = this._settings.get_images_preview_size();

        this._history.forEach(i => {
            i.setImagesPreview(enabled, size);
        });
    }

    _createRow(elementSize, slotIndex, index, uuid = null) {
        const item = new GPasteItem(this._client, elementSize, slotIndex, index, uuid);
        // Before the row's own fetch can land: the constructor only starts it,
        // so the settings are in place by the time there is a kind to act on.
        item.setImagesPreview(this._settings.get_images_preview(), this._settings.get_images_preview_size());
        // The rows live in a section that is not part of the menu's item tree
        // (it is nested in the scroll view), so close the menu on activation
        // ourselves rather than relying on the usual menu-item plumbing.
        item.connect('activate', () => this.menu.itemActivated());
        // The rows live in a nested scroll view, which does not scroll a
        // keyboard-focused child into view on its own; do it ourselves so
        // arrow-key navigation never lands on a row clipped outside the
        // viewport (and so Up from the actions reveals the last row).
        item.connect('key-focus-in', () => ensureActorVisibleInScrollView(this._scrollView, item));
        this._historySection.addMenuItem(item);
        this._history.push(item);
        return item;
    }

    _clearRows() {
        this._history.forEach(i => i.destroy());
        this._history = [];
    }

    _scrollToTop() {
        // Bringing the first row into view resets to the top; unlike poking the
        // adjustment directly it animates and cooperates with the fade effect.
        // With no row to target (empty history or no search results) there is
        // nothing to scroll to, so just pin the adjustment to the top.
        if (this._history.length > 0) {
            ensureActorVisibleInScrollView(this._scrollView, this._history[0]);
        } else {
            const adjustment = this._scrollView.vadjustment;
            adjustment.value = adjustment.lower;
        }
    }

    _loadMore() {
        if (this._loading || !this._client)
            return;

        this._loading = true;

        try {
            const elementSize = this._settings.get_element_size();
            const filtered = this._isFiltered();
            const start = this._history.length;
            const end = Math.min(this._totalSize(), start + this._fillBatch());

            for (let i = start; i < end; ++i)
                this._createRow(elementSize, i, filtered ? -1 : i, filtered ? this._filteredUuids[i] : null);
        } finally {
            // Never leave _loading stuck true on a throw, or lazy loading wedges
            // for the rest of the session.
            this._loading = false;
        }
    }

    // One batch is a viewport's worth of rows: enough to fill the visible area
    // so the menu always has something to scroll to while more history remains.
    // The average row height is derived from the laid-out content (upper /
    // loaded); before anything is laid out we fall back to a fixed count.
    _fillBatch() {
        const adjustment = this._scrollView.vadjustment;
        const page = adjustment.page_size;
        const loaded = this._history.length;

        if (page > 0 && loaded > 0 && adjustment.upper > 0) {
            const rowHeight = adjustment.upper / loaded;
            return Math.max(1, Math.ceil(page / rowHeight) + 1);
        }

        return GPasteIndicator._DEFAULT_BATCH;
    }

    _maybeLoadMore() {
        if (!this._client || this._loading || this._history.length >= this._totalSize())
            return;

        const adjustment = this._scrollView.vadjustment;
        const page = adjustment.page_size;

        if (page <= 0) // not allocated yet
            return;

        const overflowing = adjustment.upper > page;
        const atBottom = (adjustment.value + page) >= (adjustment.upper - 1);

        if (!overflowing || atBottom)
            this._loadMore();
    }

    // Ask the daemon how big the current history is and record it. Every path
    // that repopulates the list bumps _reloadGeneration on entry, so a reload
    // (or a search, or a refresh) started while we were awaiting makes this one
    // stale: drop out rather than publish an answer about a history nobody is
    // showing any more, and likewise once the client has gone.
    //
    // Returns whether the caller may carry on.
    async _fetchAvailable() {
        const generation = this._reloadGeneration;

        // Sizing asks about the current history, whichever that is. Its name is
        // only read here to tell a live daemon from one off the bus: the
        // property's cache is empty while there is none, and there is nothing
        // to size then.
        if (!this._client.get_history_name())
            return false;

        let available;

        // The name having an owner is not the daemon answering: it may go in
        // between, or the call may time out. A size that never came back is the
        // same "nothing to size" as no daemon at all, and the caller reconciles
        // either way -- an exception escaping here would skip that instead.
        try {
            available = await this._client.get_history_size();
        } catch (e) {
            console.error(e);
            return false;
        }

        if (!this._client || generation !== this._reloadGeneration)
            return false;

        this._available = available;
        return true;
    }

    async _reload() {
        if (!this._client)
            return;

        const generation = ++this._reloadGeneration;

        this._filteredUuids = [];

        if (!await this._fetchAvailable()) {
            this._reconcileConnection(generation);
            return;
        }

        this._rebuild(this._available === 0);
    }

    // A fetch gives up when there is no daemon left to ask, and nothing on that
    // path repaints the menu: _rebuild() is what calls _updateVisibility() and
    // it is not reached, so the rows and the placeholder would go on describing
    // a daemon that has gone -- or, for a reload that ran right after one came
    // back, one that is here. So the state is reconciled instead, which is the
    // same question "notify::history" answers and lands in the same place. A
    // newer reload owns it once it has moved @generation.
    //
    // Guarded like every other path resuming after an await: the indicator may
    // have been destroyed while the call was out, and _onDaemonGone() would then
    // paint destroyed actors and schedule a reconnect nothing is left to cancel.
    _reconcileConnection(generation) {
        if (this._destroyed || generation !== this._reloadGeneration)
            return;

        // A daemon that has gone is a change of state, and _onDaemonStateChanged()
        // repaints the whole menu for it. One that is still there and simply did
        // not answer is not a change of anything: giving up covers a call that
        // *failed* as much as one with nothing to ask, and the rows would go on
        // showing a history this pass could not size. So they go instead, and the
        // menu says only what it knows -- the next menu opening, update or
        // keystroke asks again.
        if (this._onDaemonStateChanged())
            return;

        this._forgetHistory();
    }

    // A search asks the daemon to match; the favourites filter asks it for the
    // pinned items; with both on, only the matches are left to sift, and those
    // are the narrow set already.
    async _runFilter() {
        if (!this._client)
            return;

        const generation = ++this._reloadGeneration;
        const search = this._searchItem.text.toLowerCase();
        let items;

        // A daemon that has gone fails the match rather than answering an empty
        // one, and nothing further down repaints the menu: reconciled here as a
        // fetch that gave up is, so a filtered list does not go on showing rows
        // for a history nobody can reach.
        try {
            items = this._hasSearch()
                ? await this._client.search(search)
                : await this._client.get_favourites();
        } catch (e) {
            console.error(e);
            this._reconcileConnection(generation);
            return;
        }

        if (!this._client || generation !== this._reloadGeneration)
            return;

        this._filteredUuids = items
            .filter(item => !this._hasSearch() || !this._searchItem.favourites || item.is_favourite())
            .map(item => item.get_uuid());
        this._rebuild(this._filteredUuids.length === 0);
    }

    // Reconcile the materialised rows with the current content (history or
    // search results) in place rather than tearing them all down: update the
    // rows we keep, drop any surplus, create any shortfall, then scroll back to
    // the top. Reusing the actors avoids the flicker (and focus loss) of
    // rebuilding the whole list on every search keystroke and when entering or
    // leaving search. Shared by the full reload and the search paths.
    _rebuild(empty) {
        // Hold off lazy loading while the row count (and thus the scroll
        // adjustment) is in flux, so a re-entrant _maybeLoadMore() can't fire.
        this._loading = true;

        try {
            const filtered = this._isFiltered();
            const total = this._totalSize();
            const elementSize = this._settings.get_element_size();

            // Keep a viewport's worth of rows, or the whole content when it is
            // smaller; never drop the rows we already loaded past that (lazy
            // loading tops the list up again on scroll).
            const target = Math.min(total, Math.max(this._history.length, this._fillBatch()));

            while (this._history.length > target)
                this._history.pop().destroy();

            for (let i = 0; i < this._history.length; ++i) {
                if (filtered)
                    this._history[i].setUuid(this._filteredUuids[i]).catch(console.error);
                else
                    this._history[i].setIndex(i).catch(console.error);
            }

            for (let i = this._history.length; i < target; ++i)
                this._createRow(elementSize, i, filtered ? -1 : i, filtered ? this._filteredUuids[i] : null);
        } finally {
            this._loading = false;
        }

        this._updateVisibility(empty);
        this._scrollToTop();
    }

    _reloadCurrent() {
        // Nothing to ask and nothing to list; re-assert what the menu says
        // instead, since this is also what every menu opening goes through.
        if (!this._connected) {
            this._updateVisibility(true);
            return;
        }

        if (this._isFiltered())
            this._runFilter().catch(console.error);
        else
            this._reload().catch(console.error);
    }

    // Reconcile the materialised rows with the current history in place rather
    // than tearing them all down and rebuilding: drop the rows past the new
    // size, re-fetch the content of those at and after @from (their items
    // shifted), then top up if the viewport gained room.
    async _refresh(from) {
        if (!this._client)
            return;

        const generation = ++this._reloadGeneration;

        if (!await this._fetchAvailable()) {
            this._reconcileConnection(generation);
            return;
        }

        const available = this._available;

        while (this._history.length > available)
            this._history.pop().destroy();

        for (let i = from; i < this._history.length; ++i)
            this._history[i].setIndex(i).catch(console.error);

        this._updateVisibility(available === 0);
        this._maybeLoadMore();
    }

    _update(client, action, target, uuid, position) {
        // A filtered list maps its rows to uuids rather than to history
        // positions, so a position says nothing to it. One item changing in
        // place is still worth catching by uuid; anything else means re-running
        // the filter. Not in a pinned-only list, though: what just changed there
        // may be the very flag that puts a row in it, and the update names the
        // item rather than what became of its flag, so it asks again.
        if (this._isFiltered()) {
            if (!this._searchItem.favourites &&
                target === GPaste.UpdateTarget.ITEM && action === GPaste.UpdateAction.REPLACE) {
                const row = this._history.find(i => i.uuid === uuid);

                if (row) {
                    row.refresh();
                    return;
                }
            }

            this._runFilter().catch(console.error);
            return;
        }

        switch (target) {
        case GPaste.UpdateTarget.ALL:
            this._refresh(0).catch(console.error);
            break;
        case GPaste.UpdateTarget.ITEM:
            switch (action) {
            case GPaste.UpdateAction.REPLACE:
                this._history[position]?.refresh();
                break;
            case GPaste.UpdateAction.REMOVE:
                this._refresh(position).catch(console.error);
                break;
            }
            break;
        }
    }

    _updateVisibility(empty) {
        // The tracking switch acts on a daemon: offered with none there it
        // would either fail unreported or quietly start one behind the ladder's
        // back, showing the state the user just set either way.
        if (this._switch)
            this._switch.visible = this._connected;

        if (!this._connected) {
            // A daemon that owns the bus name but has not answered yet is
            // starting, not missing: it owns the name before it exports
            // anything, and a migration or passphrase dialog can hold it there
            // for as long as the user takes to answer. A _connect () still
            // retrying is that same state with nothing to ask it of -- there is
            // no proxy yet, so no name to have an owner -- and a row offering a
            // retry there is one _retry () refuses, the connection in flight
            // owning the rebuild. Both hold until the ladder runs out: one that
            // never answers is not starting any more, and a row that only ever
            // says "Loading…" is not reactive and offers no way out of it.
            if ((this._connecting || this._client?.get_name_owner()) && !this._reconnectSpent)
                this._dummyHistoryItem.showLoading();
            else
                this._dummyHistoryItem.showDisconnected();

            // The menu can be opened, and this reached, while _connect () is
            // still retrying -- before _setup () has built the footer the empty
            // row comes from.
            this._emptyHistoryItem?.hide();
            this._searchItem.hide();
            return;
        }

        if (!empty) {
            this._dummyHistoryItem.hide();
            this._emptyHistoryItem.show();
            this._searchItem.show();
        } else if (this._hasSearch()) {
            this._dummyHistoryItem.showNoResult();
            this._emptyHistoryItem.hide();
            this._searchItem.show();
        } else if (this._searchItem.favourites) {
            this._dummyHistoryItem.showNoPinned();
            this._emptyHistoryItem.hide();
            this._searchItem.show();
        } else {
            this._dummyHistoryItem.showEmpty();
            this._emptyHistoryItem.hide();
            this._searchItem.hide();
        }
    }

    _popup() {
        this.menu.open(true);
    }

    _toggle(c, state) {
        this._switch.syncState(state);
    }

    _selectSearch() {
        if (this._history.length > 0)
            this._searchItem.grabFocus();
    }

    // Reported only while there is a daemon to report it to. The proxy is built
    // with G_DBUS_PROXY_FLAGS_NONE, so this is an ordinary call on an
    // activatable service: made with none there it would start the very daemon
    // the ladder refuses to start on its own, within a second of the user
    // stopping it and with nothing they could do about it short of turning the
    // extension off.
    _onStateChanged(state) {
        if (this._client && this._connected)
            this._client.report_extension_state(state, null);
    }

    _onOpenStateChanged(menu, state) {
        if (state) {
            // The menu opens on the history, so both filters go; reset() drops
            // them without announcing either, which is what leaves this one
            // reload to cover the pair rather than one round trip per filter
            // that happened to be on.
            this._searchItem.reset();
            this._reloadCurrent();
            GLib.Source.set_name_by_id(GLib.idle_add_once(GLib.PRIORITY_DEFAULT_IDLE, this._selectSearch.bind(this)), '[GPaste] select search');
        } else {
            this._updateIndexVisibility(false);
        }
        super._onOpenStateChanged(menu, state);
    }

    // The footer items are menu items and walk among themselves, but the
    // history above them is not in the menu's item tree -- its rows live in
    // a section nested in the scroll view -- so Up from the first of them has
    // nothing there to land on. Bridge that one step.
    _onMenuKeyPress(actor, event) {
        if (this._switch && this._switch.active)
            return super._onMenuKeyPress(actor, event);

        if (event.get_key_symbol() !== Clutter.KEY_Up)
            return Clutter.EVENT_PROPAGATE;

        const focus = global.stage.get_key_focus();

        if (!this._footer || !focus || !this._footer.open.contains(focus))
            return Clutter.EVENT_PROPAGATE;

        const last = this._lastHistoryItem();

        if (!last)
            return Clutter.EVENT_PROPAGATE;

        last.grab_key_focus();

        return Clutter.EVENT_STOP;
    }

    _lastHistoryItem() {
        for (let i = this._history.length - 1; i >= 0; --i) {
            if (this._history[i].visible)
                return this._history[i];
        }
        return null;
    }

    _updateScrollHeight() {
        // Size against the monitor the indicator (and thus its menu) lives on,
        // not always the primary one.
        const monitor = Main.layoutManager.findIndexForActor(this);
        const workArea = Main.layoutManager.getWorkAreaForMonitor(monitor);

        this._scrollView.style = `max-height: ${Math.floor(workArea.height * 0.6)}px`;
    }

    _onDestroy() {
        // Set here and not only in shutdown (): the actor can be destroyed by
        // other routes, and a _probeDaemon () suspended on its call would
        // otherwise resume past this and schedule a reconnect nothing is left to
        // cancel.
        this._destroyed = true;
        this._cancelReconnect();
        Main.layoutManager.disconnectObject(this);
        this._settings.disconnectObject(this);
        this._clearRows();

        if (!this._client)
            return;

        this._client.disconnectObject(this);
        this._client = null;
    }
});
