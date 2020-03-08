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
    _init(client, index) {
        super._init({ x_align: St.Align.END });
        this._deleteButton = new DeleteButton.GPasteDeleteButton(client, index);
        this.child = this._deleteButton;
    }

    setIndex(index) {
        this._deleteButton.setIndex(index);
    }
});
