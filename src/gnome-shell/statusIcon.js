// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import GObject from 'gi://GObject';
import St from 'gi://St';

export const GPasteStatusIcon = GObject.registerClass(
class GPasteStatusIcon extends St.BoxLayout {
    _init() {
        super._init({style_class: 'panel-status-menu-box'});

        this.add_child(new St.Icon({
            icon_name: 'edit-paste-symbolic',
            style_class: 'system-status-icon',
        }));
    }
});
