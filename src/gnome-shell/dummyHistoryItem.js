/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Gettext = imports.gettext;

const PopupMenu = imports.ui.popupMenu;

const _ = Gettext.domain('GPaste').gettext;

var GPasteDummyHistoryItem = class extends PopupMenu.PopupMenuItem {
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
