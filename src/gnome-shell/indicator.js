// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import './dependencies.js';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';

import {Button} from 'resource:///org/gnome/shell/ui/panelMenu.js';
import {PopupMenuSection, PopupSeparatorMenuItem} from 'resource:///org/gnome/shell/ui/popupMenu.js';

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import GLib from 'gi://GLib';
import St from 'gi://St';
import GPaste from 'gi://GPaste?version=2';

import {GPasteActions} from './actions.js';
import {GPasteDummyHistoryItem} from './dummyHistoryItem.js';
import {GPasteEmptyHistoryItem} from './emptyHistoryItem.js';
import {GPasteItem} from './item.js';
import {GPasteSearchItem} from './searchItem.js';
import {GPasteStateSwitch} from './stateSwitch.js';
import {GPasteStatusIcon} from './statusIcon.js';

export const GPasteIndicator = GObject.registerClass(
class GPasteIndicator extends Button {
    static _CONNECT_RETRIES = 3;

    constructor() {
        super(0.0, 'GPaste');

        this._statusIcon = new GPasteStatusIcon();
        this.add_child(this._statusIcon);

        this._settings = new GPaste.Settings();
        this._destroyed = false;

        // The rows currently materialised in the scrollable history section.
        // We never hold the whole history: rows are appended in batches as the
        // user scrolls (lazy loading), so St does not have to lay out thousands
        // of actors it cannot recycle.
        this._history = [];
        this._searchResults = [];
        this._available = 0;
        this._loading = false;
        this._reloadGeneration = 0;
        this._batch = this._settings.get_max_displayed_history_size();

        this._dummyHistoryItem = new GPasteDummyHistoryItem();
        this.menu.addMenuItem(this._dummyHistoryItem);

        this._searchItem = new GPasteSearchItem();
        this._searchItem.connect('text-changed', this._onNewSearch.bind(this));

        this._settings.connectObject('changed::element-size', this._resetElementSize.bind(this), this);
        this._resetElementSize();

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
        this._client = await this._connect();
        if (this._destroyed || !this._client) {
            this._client = null;
            return;
        }

        this._emptyHistoryItem = new GPasteEmptyHistoryItem(this._client, this._settings, this.menu);
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

        this.menu.addMenuItem(new PopupSeparatorMenuItem());
        this._actions = new GPasteActions(this._client, this.menu, this._emptyHistoryItem);
        this.menu.addMenuItem(this._actions);

        const dummyIndex = this.menu.box.get_children().indexOf(this._dummyHistoryItem);
        this.menu.box.insert_child_at_index(this._scrollView, dummyIndex + 1);

        this._updateScrollHeight();

        // "changed" fires when the content/viewport is resized (fill until the
        // viewport overflows); "notify::value" fires on scroll (load the next
        // batch when the bottom is reached).
        this._scrollView.vadjustment.connectObject(
            'changed', this._maybeLoadMore.bind(this),
            'notify::value', this._maybeLoadMore.bind(this),
            this);

        await this._reload();
        if (this._destroyed) {
            this._client = null;
            return;
        }

        this._settings.connectObject('changed::max-displayed-history-size', () => {
            this._batch = this._settings.get_max_displayed_history_size();
            this._reloadCurrent();
        }, this);

        this._client.connectObject(
            'update', this._update.bind(this),
            'show-history', this._popup.bind(this),
            'tracking', this._toggle.bind(this),
            this);

        this._onStateChanged(true);

        // The ctrl-index overlay and ctrl+0-9 selection are driven by raw key
        // events. The menu object is a Signals.EventEmitter and never emits
        // 'key-press-event'/'key-release-event'; those fire on its actor, so the
        // handlers have to be connected there.
        this.menu.actor.connect('key-press-event', this._onKeyPressEvent.bind(this));
        this.menu.actor.connect('key-release-event', this._onKeyReleaseEvent.bind(this));

        this.connect('destroy', this._onDestroy.bind(this));
    }

    shutdown() {
        this._destroyed = true;
        this._onStateChanged(false);
        // destroy() fires the 'destroy' signal connected in _setup, which runs
        // _onDestroy(); don't call it a second time here.
        this.destroy();
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

    _totalSize() {
        return this._hasSearch() ? this._searchResults.length : this._available;
    }

    _onNewSearch() {
        this._reloadCurrent();
    }

    _resetElementSize() {
        const size = this._settings.get_element_size();
        this._searchItem.resetSize(size / 2 + 3);
        this._history.forEach(i => {
            i.setTextSize(size);
        });
    }

    _createRow(elementSize, slotIndex, index, uuid = null) {
        const item = new GPasteItem(this._client, elementSize, slotIndex, index, uuid);
        // The rows live in a section that is not part of the menu's item tree
        // (it is nested in the scroll view), so close the menu on activation
        // ourselves rather than relying on the usual menu-item plumbing.
        item.connect('activate', () => this.menu.itemActivated());
        this._historySection.addMenuItem(item);
        this._history.push(item);
        return item;
    }

    _clearRows() {
        this._history.forEach(i => i.destroy());
        this._history = [];
    }

    _scrollToTop() {
        const adjustment = this._scrollView.vadjustment;
        adjustment.value = adjustment.lower;
    }

    _loadMore() {
        if (this._loading || !this._client)
            return;

        this._loading = true;

        try {
            const elementSize = this._settings.get_element_size();
            const searching = this._hasSearch();
            const start = this._history.length;
            const end = Math.min(this._totalSize(), start + this._batch);

            for (let i = start; i < end; ++i)
                this._createRow(elementSize, i, searching ? -1 : i, searching ? this._searchResults[i] : null);
        } finally {
            // Never leave _loading stuck true on a throw, or lazy loading wedges
            // for the rest of the session.
            this._loading = false;
        }
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

    async _reload() {
        if (!this._client)
            return;

        const generation = ++this._reloadGeneration;

        this._searchResults = [];

        const name = await this._client.get_history_name();
        if (!this._client || generation !== this._reloadGeneration)
            return;
        this._available = await this._client.get_history_size(name);
        if (!this._client || generation !== this._reloadGeneration)
            return;

        this._clearRows();
        this._scrollToTop();
        this._loadMore();
        this._updateVisibility(this._available === 0);
    }

    async _runSearch() {
        if (!this._client)
            return;

        const generation = ++this._reloadGeneration;
        const search = this._searchItem.text.toLowerCase();

        this._searchResults = await this._client.search(search);
        if (!this._client || generation !== this._reloadGeneration)
            return;

        this._clearRows();
        this._scrollToTop();
        this._loadMore();
        this._updateVisibility(this._searchResults.length === 0);
    }

    _reloadCurrent() {
        if (this._hasSearch())
            this._runSearch().catch(console.error);
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

        const name = await this._client.get_history_name();
        if (!this._client || generation !== this._reloadGeneration)
            return;
        this._available = await this._client.get_history_size(name);
        if (!this._client || generation !== this._reloadGeneration)
            return;

        while (this._history.length > this._available)
            this._history.pop().destroy();

        for (let i = from; i < this._history.length; ++i)
            this._history[i].setIndex(i).catch(console.error);

        this._updateVisibility(this._available === 0);
        this._maybeLoadMore();
    }

    _update(client, action, target, position) {
        // While searching, the visible rows map to search results rather than
        // history positions, so re-run the search instead.
        if (this._hasSearch()) {
            this._runSearch().catch(console.error);
            return;
        }

        switch (target) {
        case GPaste.UpdateTarget.ALL:
            this._refresh(0).catch(console.error);
            break;
        case GPaste.UpdateTarget.POSITION:
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
        if (!empty) {
            this._dummyHistoryItem.hide();
            this._emptyHistoryItem.show();
            this._searchItem.show();
        } else if (this._hasSearch()) {
            this._dummyHistoryItem.showNoResult();
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
        this._switch.toggle(state);
    }

    _selectSearch() {
        if (this._history.length > 0)
            this._searchItem.grabFocus();
    }

    _onStateChanged(state) {
        if (this._client)
            this._client.on_extension_state_changed(state, null);
    }

    _onOpenStateChanged(menu, state) {
        if (state) {
            this._searchItem.reset();
            this._reloadCurrent();
            GLib.Source.set_name_by_id(GLib.idle_add_once(GLib.PRIORITY_DEFAULT_IDLE, this._selectSearch.bind(this)), '[GPaste] select search');
        } else {
            this._updateIndexVisibility(false);
        }
        super._onOpenStateChanged(menu, state);
    }

    _onMenuKeyPress(actor, event) {
        if (this._switch && this._switch.active)
            return super._onMenuKeyPress(actor, event);

        // When an action button is focused, Up returns to the last history
        // item; the action buttons are plain St.Buttons and don't drive that
        // focus navigation themselves. (Left/Right between the actions is
        // handled by GPasteActions itself, closer to the focused button.)
        const focus = global.stage.get_key_focus();
        if (event.get_key_symbol() === Clutter.KEY_Up &&
            this._actions && focus && this._actions.contains(focus)) {
            const last = this._lastHistoryItem();
            if (last) {
                last.grab_key_focus();
                return Clutter.EVENT_STOP;
            }
        }

        return Clutter.EVENT_PROPAGATE;
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
        this._settings.disconnectObject(this);
        this._clearRows();

        if (!this._client)
            return;

        this._client.disconnectObject(this);
        this._client = null;
    }
});
