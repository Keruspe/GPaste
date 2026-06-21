// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

import {GPasteIndicator} from './indicator.js';

export default class GPasteExtension extends Extension {
    enable() {
        Main.panel.addToStatusArea('gpaste', new GPasteIndicator());
    }

    disable() {
        Main.panel.statusArea.gpaste.shutdown();
    }
}
