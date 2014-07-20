/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Gettext = imports.gettext;
const Lang = imports.lang;

const PanelMenu = imports.ui.panelMenu;
const PopupMenu = imports.ui.popupMenu;

const GPaste = imports.gi.GPaste;

const _ = Gettext.domain('GPaste').gettext;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();

const AboutItem = Me.imports.aboutItem;
const DummyHistoryItem = Me.imports.dummyHistoryItem;
const EmptyHistoryItem = Me.imports.emptyHistoryItem;
const Item = Me.imports.item;
const SearchItem = Me.imports.searchItem;
const StateSwitch = Me.imports.stateSwitch;
const StatusIcon = Me.imports.statusIcon;

const GPasteIndicator = new Lang.Class({
    Name: 'GPasteIndicator',
    Extends: PanelMenu.Button,

    _init: function() {
        this.parent(0.0, "GPaste");

        this.actor.add_child(new StatusIcon.GPasteStatusIcon());

        this._settings = new GPaste.Settings();

        this._headerSize = 0;
        this._postHeaderSize = 0;
        this._history = [];
        this._preFooterSize = 0;
        this._footerSize = 0;

        this._dummyHistoryItem = new DummyHistoryItem.GPasteDummyHistoryItem();

        this._searchItem = new SearchItem.GPasteSearchItem();
        this._setMaxDisplayedSize();
        this._settingsMaxSizeChangedId = this._settings.connect('changed::max-displayed-history-size', Lang.bind(this, this._resetMaxDisplayedSize));
        this._searchItem.connect('text-changed', Lang.bind(this, this._onSearch));

        this._settingsSizeChangedId = this._settings.connect('changed::element-size', Lang.bind(this, this._resetEntrySize));
        this._resetEntrySize();
        this.menu.connect('open-state-changed', Lang.bind(this, this._onOpenStateChanged));

        this._addToPostHeader(new PopupMenu.PopupSeparatorMenuItem());
        this._addToPostHeader(this._dummyHistoryItem);
        this._addToPreFooter(new PopupMenu.PopupSeparatorMenuItem());

        GPaste.Client.new(Lang.bind(this, function (obj, result) {
            this._client = GPaste.Client.new_finish(result);

            this._dummyHistoryItem.update();
            this._emptyHistoryItem = new EmptyHistoryItem.GPasteEmptyHistoryItem(this._client);

            this._addToHeader(new StateSwitch.GPasteStateSwitch(this._client));
            this._addToHeader(this._searchItem);
            this._addToFooter(this._emptyHistoryItem);
            this._addSettingsAction();
            this._addToFooter(new AboutItem.GPasteAboutItem(this._client));

            this._clientChangedId = this._client.connect('changed', Lang.bind(this, this._refresh));
            this._refresh();

            this._clientShowId = this._client.connect('show-history', Lang.bind(this, this._popup));

            this._onStateChanged (true);

            this.actor.connect('destroy', Lang.bind(this, this._onDestroy));
        }));
    },

    shutdown: function() {
        this._onStateChanged (false);
        this.destroy();
    },

    _onSearch: function() {
        let search = this._searchItem.text.toLowerCase();
        let maxSize = this._maxSize;
        let i = 0;

        this._history.map(function(item) {
            if (i < maxSize) {
                if (item.match(search)) {
                    ++i;
                    item.actor.show();
                    return;
                }
            }
            item.actor.hide();
        });
    },

    _resetEntrySize: function() {
        this._searchItem.resetSize(this._settings.get_element_size()/2);
    },

    _setMaxDisplayedSize: function() {
        this._maxSize = this._settings.get_max_displayed_history_size();
    },

    _resetMaxDisplayedSize: function() {
        this._setMaxDisplayedSize();
        this._onSearch();
    },

    _refresh: function() {
        this._client.get_history_size(Lang.bind(this, function(client, result) {
            let size = client.get_history_size_finish(result);
            this._updateVisibility(size == 0);
            for (let index = this._history.length; index < size; ++index) {
                let item = new Item.GPasteItem(this._client, this._settings, index);
                item.connect('changed', Lang.bind(this, this._onSearch));
                this._addToHistory(item);
            }
            for (let index = size, length = this._history.length; index < length; ++index) {
                this._history.pop().destroy();
            }
            this._onSearch();
        }));
    },

    _updateVisibility: function(empty) {
        if (empty) {
            this._dummyHistoryItem.actor.show();
            this._emptyHistoryItem.actor.hide();
            this._searchItem.actor.hide();
        } else {
            this._dummyHistoryItem.actor.hide();
            this._emptyHistoryItem.actor.show();
            this._searchItem.actor.show();
        }
    },

    _popup: function() {
        this.menu.open(true);
        this._selectSearch(true);
    },

    _selectSearch: function(active) {
        if (this._history.length > 0) {
            this._searchItem.setActive(active);
        }
    },

    _addToHeader: function(item) {
        this.menu.addMenuItem(item, this._headerSize++);
    },

    _addToPostHeader: function(item) {
        this.menu.addMenuItem(item, this._headerSize + this._postHeaderSize++);
    },

    _addToHistory: function(item) {
        this.menu.addMenuItem(item, this._headerSize + this._postHeaderSize + this._history.length);
        this._history[this._history.length] = item;
    },

    _addToPreFooter: function(item) {
        this.menu.addMenuItem(item, this._headerSize + this._postHeaderSize + this._history.length + this._preFooterSize++);
    },

    _addToFooter: function(item) {
        this.menu.addMenuItem(item, this._headerSize + this._postHeaderSize + this._history.length + this._preFooterSize + this._footerSize++);
    },

    _addSettingsAction: function() {
        // Simulate _addToFooter
        this.menu.addSettingsAction(_("GPaste daemon settings"), 'org.gnome.GPaste.Settings.desktop');
        ++this._footerSize;
    },

    _onStateChanged: function(state) {
        this._client.on_extension_state_changed(state, null);
    },

    _onOpenStateChanged: function(menu, state) {
        if (state) {
            this._searchItem.reset();
        }
    },

    _onDestroy: function() {
        this._client.disconnect(this._clientChangedId);
        this._client.disconnect(this._clientShowId);
        this._settings.disconnect(this._settingsMaxSizeChangedId);
        this._settings.disconnect(this._settingsSizeChangedId);
    }
});

