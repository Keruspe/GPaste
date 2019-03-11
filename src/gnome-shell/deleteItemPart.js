/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const { St } = imports.gi;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();

const DeleteButton = Me.imports.deleteButton;

class GPasteDeleteItemPart {
    constructor(client, index) {
        this.actor = new St.Bin({ x_align: St.Align.END });
        this._deleteButton = new DeleteButton.GPasteDeleteButton(client, index);
        this.actor.child = this._deleteButton.actor;
    }

    setIndex(index) {
        this._deleteButton.setIndex(index);
    }
};
