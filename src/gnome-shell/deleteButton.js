// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';

import GObject from 'gi://GObject';
import St from 'gi://St';

// The look of a delete action and nothing of what it deletes: an item row and a
// history row both wear it, and each answers 'clicked' with its own call -- the
// very call its Delete key makes, so the two can never disagree about what a
// row with nothing to delete yet does.
export const GPasteDeleteButton = GObject.registerClass(
class GPasteDeleteButton extends St.Button {
    constructor() {
        // Focusable, which St.Button is not on its own: an item row shows its
        // actions while the keyboard is on one of them, and hides what the
        // keyboard cannot reach. See GPasteItem.
        super({can_focus: true});

        this.child = new St.Icon({
            icon_name: 'edit-delete-symbolic',
            style_class: 'popup-menu-icon',
        });
        // An icon names the button for the eye and for nothing else.
        this.accessible_name = _('Delete');
    }
});
