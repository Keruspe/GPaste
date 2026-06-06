// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import './dependencies.js';

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';
import {Button} from 'resource:///org/gnome/shell/ui/panelMenu.js';
import {PopupSeparatorMenuItem} from 'resource:///org/gnome/shell/ui/popupMenu.js';

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import GLib from 'gi://GLib';
import GPaste from 'gi://GPaste?version=2';

import {GPasteActions} from './actions.js';
import {GPasteDummyHistoryItem} from './dummyHistoryItem.js';
import {GPasteEmptyHistoryItem} from './emptyHistoryItem.js';
import {GPasteItem} from './item.js';
import {GPastePageSwitcher} from './pageSwitcher.js';
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

        this._headerSize = 0;
        this._postHeaderSize = 0;
        this._history = [];
        this._preFooterSize = 0;
        this._footerSize = 0;

        this._searchResults = [];

        this._dummyHistoryItem = new GPasteDummyHistoryItem();

        this._searchItem = new GPasteSearchItem();
        this._searchItem.connect('text-changed', this._onNewSearch.bind(this));

        this._settings.connectObject('changed::element-size', this._resetElementSize.bind(this), this);
        this._resetElementSize();

        this.menu.connect('key-press-event', this._onMenuKeyPress.bind(this));

        this._pageSwitcher = new GPastePageSwitcher();
        this._pageSwitcher.connect('switch', (sw, page) => {
            this._updatePage(page);
        });

        this._addToPostHeader(this._dummyHistoryItem);
        this._addToPreFooter(new PopupSeparatorMenuItem());

        this._setup().catch(console.error);
    }

    async _connect(retries = GPasteIndicator._CONNECT_RETRIES, delay = 1) {
        try {
            return await GPaste.Client.new(null);
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

        this._addToHeader(this._switch);
        this._addToHeader(this._searchItem);
        this._addToHeader(this._pageSwitcher);
        this._addToFooter(new GPasteActions(this._client, this.menu, this._emptyHistoryItem));

        await this._resetMaxDisplayedSize();
        if (this._destroyed) {
            this._client = null;
            return;
        }

        this._settings.connectObject('changed::max-displayed-history-size', () => this._resetMaxDisplayedSize().catch(console.error), this);

        this._client.connectObject(
            'update', this._update.bind(this),
            'show-history', this._popup.bind(this),
            'tracking', this._toggle.bind(this),
            this);

        this._onStateChanged(true);

        this.menu.connect('key-press-event', this._onKeyPressEvent.bind(this));
        this.menu.connect('key-release-event', this._onKeyReleaseEvent.bind(this));

        this.connect('destroy', this._onDestroy.bind(this));
    }

    shutdown() {
        this._destroyed = true;
        this._onStateChanged(false);
        this._onDestroy();
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

    async _onSearch(page) {
        if (this._hasSearch()) {
            const search = this._searchItem.text.toLowerCase();
            this._searchResults = await this._client.search(search, null);
            let results = this._searchResults.length;
            const maxSize = this._history.length;

            if (!this._pageSwitcher.updateForSize(results))
                return;


            this._pageSwitcher.setActive(page);
            const offset = this._pageSwitcher.getPageOffset();

            if (results > (maxSize + offset))
                results = maxSize + offset;


            this._history.slice(0, results - offset).forEach((i, index) => {
                i.setUuid(this._searchResults[offset + index]);
            });

            this._updateVisibility(results === 0);

            this._history.slice(results - offset, maxSize).forEach(i => {
                i.setIndex(-1);
            });
        } else {
            this._searchResults = [];
            await this._refresh(0);
        }
    }

    _onNewSearch() {
        this._onSearch(1).catch(console.error);
    }

    _resetElementSize() {
        const size = this._settings.get_element_size();
        this._searchItem.resetSize(size / 2 + 3);
        this._history.forEach(i => {
            i.setTextSize(size);
        });
    }

    _updatePage(page) {
        this._pageSwitcher.setActive(page);
        this._refresh(0).catch(console.error);
    }

    async _resetMaxDisplayedSize() {
        const oldSize = this._history.length;
        const newSize = this._settings.get_max_displayed_history_size();
        const elementSize = this._settings.get_element_size();

        this._pageSwitcher.setMaxDisplayedSize(newSize);

        const name = await this._client.get_history_name(null);
        if (!this._client)
            return;
        const realSize = await this._client.get_history_size(name, null);
        const offset = this._pageSwitcher.getPageOffset();

        if (newSize > oldSize) {
            for (let index = oldSize; index < newSize; ++index) {
                const realIndex = index + offset;
                const item = new GPasteItem(this._client, elementSize, index, realIndex < realSize ? realIndex : -1);
                this.menu.addMenuItem(item, this._headerSize + this._postHeaderSize + index);
                this._history[index] = item;
            }
        } else {
            for (let i = newSize; i < oldSize; ++i)
                this._history.pop().destroy();
        }

        if (offset === 0 || oldSize === 0)
            this._updatePage(1);
        else
            this._updatePage((offset / oldSize) + 1);
    }

    _update(client, action, target, position) {
        switch (target) {
        case GPaste.UpdateTarget.ALL:
            this._refresh(0).catch(console.error);
            break;
        case GPaste.UpdateTarget.POSITION: {
            const offset = this._pageSwitcher.getPageOffset();
            const displayPos = position - offset;
            switch (action) {
            case GPaste.UpdateAction.REPLACE:
                this._history[displayPos].refresh();
                break;
            case GPaste.UpdateAction.REMOVE:
                this._refresh(displayPos).catch(console.error);
                break;
            }
            break;
        }
        }
    }

    async _refresh(resetTextFrom) {
        if (!this._client)
            return;
        if (this._searchResults.length > 0) {
            await this._onSearch(this._pageSwitcher.getPage());
        } else if (this._hasSearch()) {
            this._history.forEach(i => {
                i.setIndex(-1);
            });
            this._updateVisibility(true);
        } else {
            const name = await this._client.get_history_name(null);
            if (!this._client)
                return;
            const realSize = await this._client.get_history_size(name, null);

            if (!this._pageSwitcher.updateForSize(realSize))
                return;


            const maxSize = this._history.length;
            const offset = this._pageSwitcher.getPageOffset();
            const size = Math.min(realSize - offset, maxSize);

            this._history.slice(resetTextFrom, size).forEach((i, index) => {
                i.setIndex(offset + resetTextFrom + index);
            });
            this._history.slice(size, maxSize).forEach(i => {
                i.setIndex(-1);
            });

            this._updateVisibility(size === 0);
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

    _addToHeader(item) {
        this.menu.addMenuItem(item, this._headerSize++);
    }

    _addToPostHeader(item) {
        this.menu.addMenuItem(item, this._headerSize + this._postHeaderSize++);
    }

    _addToPreFooter(item) {
        this.menu.addMenuItem(item, this._headerSize + this._postHeaderSize + this._history.length + this._preFooterSize++);
    }

    _addToFooter(item) {
        this.menu.addMenuItem(item, this._headerSize + this._postHeaderSize + this._history.length + this._preFooterSize + this._footerSize++);
    }

    _onStateChanged(state) {
        if (this._client)
            this._client.on_extension_state_changed(state, null);
    }

    _onOpenStateChanged(menu, state) {
        if (state) {
            this._searchItem.reset();
            this._updatePage(1);
            GLib.Source.set_name_by_id(GLib.idle_add_once(this._selectSearch.bind(this)), '[GPaste] select search');
        } else {
            this._updateIndexVisibility(false);
        }
        super._onOpenStateChanged(menu, state);
    }

    _onMenuKeyPress(actor, event) {
        if (this._switch && this._switch.active)
            return super._onMenuKeyPress(actor, event);

        const symbol = event.get_key_symbol();
        if (symbol === Clutter.KEY_Left)
            return this._pageSwitcher.previous();

        if (symbol === Clutter.KEY_Right)
            return this._pageSwitcher.next();

        return Clutter.EVENT_PROPAGATE;
    }

    _onDestroy() {
        this._settings.disconnectObject(this);

        if (!this._client)
            return;

        this._client.disconnectObject(this);
        this._client = null;
    }
});

