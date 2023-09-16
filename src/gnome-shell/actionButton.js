/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2023, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import St from 'gi://St';

import { GPasteActionButtonActor } from './actionButtonActor.js';

export const GPasteActionButton = GObject.registerClass(
class GPasteActionButton extends St.Button {
    _init(iconName, label, action) {
        super._init({
            x_expand: true,
            x_align: Clutter.ActorAlign.CENTER,
            reactive: true,
            can_focus: true,
            track_hover: true,
            style_class: 'button',
            child: new GPasteActionButtonActor(iconName, label)
        });

        this.connect('clicked', action);
    }
});
