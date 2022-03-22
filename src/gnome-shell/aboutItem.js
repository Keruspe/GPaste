/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();
const _ = ExtensionUtils.gettext;

const { GObject, St } = imports.gi;

const ActionButtonActor = Me.imports.actionButtonActor;

var GPasteAboutItem = GObject.registerClass(
class GPasteAboutItem extends St.Button {
    _init(client, menu) {
        super._init({
            x_expand: true,
            x_align: St.Align.MIDDLE,
            reactive: true,
            can_focus: true,
            track_hover: true,
            style_class: 'button',
            child: new ActionButtonActor.GPasteActionButtonActor('dialog-information-symbolic', _("About"))
        });

        this.connect('clicked', function() {
            menu.itemActivated();
            client.about(null);
        });
    }
});
