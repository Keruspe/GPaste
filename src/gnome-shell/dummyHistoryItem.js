/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const Gettext = imports.gettext;

const PopupMenu = imports.ui.popupMenu;

const _ = Gettext.domain('GPaste').gettext;

class GPasteDummyHistoryItem extends PopupMenu.PopupMenuItem {
    constructor() {
        super(_("(Couldn't connect to GPaste daemon)"));
        this.setSensitive(false);
    }

    showEmpty() {
        this.label.text = _("(Empty)");
        this.actor.show();
    }

    showNoResult() {
        this.label.text = _("(No result)");
        this.actor.show();
    }
};
