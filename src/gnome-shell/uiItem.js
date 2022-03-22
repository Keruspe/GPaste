/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();
const _ = ExtensionUtils.gettext;

const { GObject, GPaste, St } = imports.gi;

const ActionButtonActor = Me.imports.actionButtonActor;

var GPasteUiItem = GObject.registerClass(
class GPasteUiItem extends St.Button {
    _init(menu) {
        super._init({
            x_expand: true,
            x_align: St.Align.MIDDLE,
            reactive: true,
            can_focus: true,
            track_hover: true,
            style_class: 'button',
            child: new ActionButtonActor.GPasteActionButtonActor('go-home-symbolic', _("Graphical tool"))
        });

        this.connect('clicked', function() {
            menu.itemActivated();
            GPaste.util_spawn('Ui');
        });
    }
});
