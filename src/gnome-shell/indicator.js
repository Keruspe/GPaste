/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const Gettext = imports.gettext;

const PanelMenu = imports.ui.panelMenu;
const PopupMenu = imports.ui.popupMenu;

const { Clutter, GObject, GLib, GPaste, St } = imports.gi;

const _ = Gettext.domain('GPaste').gettext;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();

const AboutItem = Me.imports.aboutItem;
const DummyHistoryItem = Me.imports.dummyHistoryItem;
const EmptyHistoryItem = Me.imports.emptyHistoryItem;
const Item = Me.imports.item;
const PageSwitcher = Me.imports.pageSwitcher;
const SearchItem = Me.imports.searchItem;
const StateSwitch = Me.imports.stateSwitch;
const StatusIcon = Me.imports.statusIcon;
const UiItem = Me.imports.uiItem;

var GPasteIndicator = GObject.registerClass(
class GPasteIndicator extends PanelMenu.Button {
    _init() {
        super._init(0.0, "GPaste");

        this._statusIcon = new StatusIcon.GPasteStatusIcon();
        this.add_child(this._statusIcon);

        this._settings = new GPaste.Settings();

        this._headerSize = 0;
        this._postHeaderSize = 0;
        this._history = [];
        this._preFooterSize = 0;
        this._footerSize = 0;

        this._searchResults = [];

        this._dummyHistoryItem = new DummyHistoryItem.GPasteDummyHistoryItem();

        this._searchItem = new SearchItem.GPasteSearchItem();
        this._searchItem.connect('text-changed', this._onNewSearch.bind(this));

        this._settingsSizeChangedId = this._settings.connect('changed::element-size', this._resetElementSize.bind(this));
        this._resetElementSize();

        this.menu.connect('open-state-changed', this._onOpenStateChanged.bind(this));
        this.menu.connect('key-press-event', this._onMenuKeyPress.bind(this));

        this._pageSwitcher = new PageSwitcher.GPastePageSwitcher();
        this._pageSwitcher.connect('switch', (sw, page) => {
            this._updatePage(page);
        });

        this._actions = new PopupMenu.PopupBaseMenuItem({
            reactive: false,
            can_focus: false
        });
        this._actions._ornamentLabel.set_x_expand(true);

        this._addToPostHeader(this._dummyHistoryItem);
        this._addToPreFooter(new PopupMenu.PopupSeparatorMenuItem());
        this._addToFooter(this._actions);

        GPaste.Client.new((obj, result) => {
            this._client = GPaste.Client.new_finish(result);

            this._uiItem = new UiItem.GPasteUiItem(this.menu);
            this._emptyHistoryItem = new EmptyHistoryItem.GPasteEmptyHistoryItem(this._client, this._settings, this.menu);
            this._aboutItem = new AboutItem.GPasteAboutItem(this._client, this.menu);
            this._switch = new StateSwitch.GPasteStateSwitch(this._client);

            this._addToHeader(this._switch);
            this._addToHeader(this._searchItem);
            this._addToHeader(this._pageSwitcher);

            this._actions.add_child(this._uiItem);
            this._actions.add_child(this._emptyHistoryItem);
            this._actions.add_child(this._aboutItem);

            this._settingsMaxSizeChangedId = this._settings.connect('changed::max-displayed-history-size', this._resetMaxDisplayedSize.bind(this));
            this._resetMaxDisplayedSize();

            this._clientUpdateId = this._client.connect('update', this._update.bind(this));
            this._clientShowId = this._client.connect('show-history', this._popup.bind(this));
            this._clientTrackingId = this._client.connect('tracking', this._toggle.bind(this));

            this._onStateChanged (true);

            this.menu.actor.connect('key-press-event', this._onKeyPressEvent.bind(this));
            this.menu.actor.connect('key-release-event', this._onKeyReleaseEvent.bind(this));

            this.connect('destroy', this._onDestroy.bind(this));
        });
    }

    shutdown() {
        this._onStateChanged (false);
        this._onDestroy();
        this.destroy();
    }

    _onKeyPressEvent(actor, event) {
        if (event.has_control_modifier()) {
            const nb = parseInt(event.get_key_unicode());
            if (!isNaN(nb) && nb >= 0 && nb <= 9 && nb < this._history.length) {
                this._history[nb].activate(event);
            }
        } else {
            this._maybeUpdateIndexVisibility(event, true);
        }
    }

    _onKeyReleaseEvent(actor, event) {
        this._updateIndexVisibility(!this._eventIsControlKey(event) && event.has_control_modifier());
    }

    _maybeUpdateIndexVisibility(event, state) {
        if (this._eventIsControlKey(event)) {
            this._updateIndexVisibility(state);
        }
    }

    _updateIndexVisibility(state) {
        this._history.slice(0, 10).forEach(function(i) {
            i.showIndex(state);
        });
    }

    _eventIsControlKey(event) {
        const key = event.get_key_symbol();
        return (key == Clutter.KEY_Control_L || key == Clutter.KEY_Control_R);
    }

    _hasSearch() {
        return this._searchItem.text.length > 0;
    }

    _onSearch(page) {
        if (this._hasSearch()) {
            const search = this._searchItem.text.toLowerCase();

            this._client.search(search, (client, result) => {
                this._searchResults = client.search_finish(result);
                let results = this._searchResults.length;
                const maxSize = this._history.length;

                if (!this._pageSwitcher.updateForSize(results)) {
                    return;
                }

                this._pageSwitcher.setActive(page);
                const offset = this._pageSwitcher.getPageOffset();

                if (results > (maxSize + offset)) {
                    results = (maxSize + offset);
                }

                this._history.slice(0, results - offset).forEach((i, index) => {
                    i.setUuid(this._searchResults[offset + index]);
                });

                this._updateVisibility(results == 0);

                this._history.slice(results - offset, maxSize).forEach(function(i) {
                    i.setIndex(-1);
                });
            });
        } else {
            this._searchResults = [];
            this._refresh(0);
        }
    }

    _onNewSearch() {
        this._onSearch(1);
    }

    _resetElementSize() {
        const size = this._settings.get_element_size();
        this._searchItem.resetSize(size/2 + 3);
        this._history.forEach(function(i) {
            i.setTextSize(size);
        });
    }

    _updatePage(page) {
        this._pageSwitcher.setActive(page);
        this._refresh(0);
    }

    _resetMaxDisplayedSize() {
        const oldSize = this._history.length;
        const newSize = this._settings.get_max_displayed_history_size();
        const elementSize = this._settings.get_element_size();

        this._pageSwitcher.setMaxDisplayedSize(newSize);

        this._client.get_history_name((client, result) => {
            const name = client.get_history_name_finish(result);

            this._client.get_history_size(name, (client, result) => {
                const offset = this._pageSwitcher.getPageOffset();

                if (newSize > oldSize) {
                    const realSize = client.get_history_size_finish(result);

                    for (let index = oldSize; index < newSize; ++index) {
                        let realIndex = index + offset;
                        let item = new Item.GPasteItem(this._client, elementSize, (realIndex < realSize) ? realIndex : -1);
                        this.menu.addMenuItem(item, this._headerSize + this._postHeaderSize + index);
                        this._history[index] = item;
                    }
                } else {
                    for (let i = newSize; i < oldSize; ++i) {
                        this._history.pop().destroy();
                    }
                }

                if (offset === 0 || oldSize === 0) {
                    this._updatePage(1);
                } else {
                    this._updatePage((offset / oldSize) + 1);
                }
            });
        });
    }

    _update(client, action, target, position) {
        switch (target) {
        case GPaste.UpdateTarget.ALL:
            this._refresh(0);
            break;
        case GPaste.UpdateTarget.POSITION:
            const offset = this._pageSwitcher.getPageOffset();
            const displayPos = position - offset;
            switch (action) {
            case GPaste.UpdateAction.REPLACE:
                this._history[displayPos].refresh();
                break;
            case GPaste.UpdateAction.REMOVE:
                this._refresh(displayPos);
                break;
            }
            break;
        }
    }

    _refresh(resetTextFrom) {
        if (this._searchResults.length > 0) {
            this._onSearch(this._pageSwitcher.getPage());
        } else if (this._hasSearch()) {
            this._history.forEach(function(i, index) {
                i.setIndex(-1);
            });
            this._updateVisibility(true);
        } else {
            this._client.get_history_name((client, result) => {
                const name = client.get_history_name_finish(result);

                this._client.get_history_size(name, (client, result) => {
                    const realSize = client.get_history_size_finish(result);

                    if (!this._pageSwitcher.updateForSize(realSize)) {
                        return;
                    }

                    const maxSize = this._history.length;
                    const offset = this._pageSwitcher.getPageOffset();
                    const size = Math.min(realSize - offset, maxSize);

                    this._history.slice(resetTextFrom, size).forEach(function(i, index) {
                        i.setIndex(offset + resetTextFrom + index);
                    });
                    this._history.slice(size, maxSize).forEach(function(i, index) {
                        i.setIndex(-1);
                    });

                    this._updateVisibility(size == 0);
                });
            });
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
        if (this._history.length > 0) {
            this._searchItem.grabFocus();
        }
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
        if (this._client) {
            this._client.on_extension_state_changed(state, null);
        }
    }

    _onOpenStateChanged(menu, state) {
        if (state) {
            this._searchItem.reset();
            this._updatePage(1);
            let id = GLib.idle_add(GLib.PRIORITY_DEFAULT, this._selectSearch.bind(this));
            GLib.Source.set_name_by_id(id, '[GPaste] select search');
        } else {
            this._updateIndexVisibility(false);
        }
        super._onOpenStateChanged(menu, state);
    }

    _onMenuKeyPress(actor, event) {
        if(this._switch.active)
            return super._onMenuKeyPress(actor, event);

        const symbol = event.get_key_symbol();
        if (symbol == Clutter.KEY_Left) {
            return this._pageSwitcher.previous();
        }
        if (symbol == Clutter.KEY_Right) {
            return this._pageSwitcher.next();
        }
        return Clutter.EVENT_PROPAGATE;
    }

    _onDestroy() {
        if (!this._client) {
            return;
        }
        this._client.disconnect(this._clientUpdateId);
        this._client.disconnect(this._clientShowId);
        this._client.disconnect(this._clientTrackingId);
        this._client = null;
        this._settings.disconnect(this._settingsMaxSizeChangedId);
        this._settings.disconnect(this._settingsSizeChangedId);
    }
});

