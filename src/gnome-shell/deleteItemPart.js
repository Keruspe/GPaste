/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const { GObject, St } = imports.gi;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();

const DeleteButton = Me.imports.deleteButton;

var GPasteDeleteItemPart = GObject.registerClass(
class GPasteDeleteItemPart extends St.Bin {
    _init(client, uuid) {
        super._init({ x_align: St.Align.END });
        this._deleteButton = new DeleteButton.GPasteDeleteButton(client, uuid);
        this.child = this._deleteButton;
    }

    setUuid(uuid) {
        this._deleteButton.setUuid(uuid);
    }
});
