/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2023, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import GObject from 'gi://GObject';
import St from 'gi://St';

export const GPasteStatusIcon = GObject.registerClass(
class GPasteStatusIcon extends St.BoxLayout {
    _init() {
        super._init({ style_class: 'panel-status-menu-box' });

        this.add_child(new St.Icon({
            icon_name: 'edit-paste-symbolic',
            style_class: 'system-status-icon'
        }));
    }
});
