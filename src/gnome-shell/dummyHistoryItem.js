/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const Gettext = imports.gettext;

const { GObject } = imports.gi;

const PopupMenu = imports.ui.popupMenu;

const _ = Gettext.domain('GPaste').gettext;

var GPasteDummyHistoryItem = GObject.registerClass(
class GPasteDummyHistoryItem extends PopupMenu.PopupMenuItem {
    _init() {
        super._init(_("(Couldn't connect to GPaste daemon)"));
        this.setSensitive(false);
    }

    showEmpty() {
        this.label.text = _("(Empty)");
        this.show();
    }

    showNoResult() {
        this.label.text = _("(No result)");
        this.show();
    }
});
