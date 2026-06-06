// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {ExtensionPreferences, gettext as _} from 'resource:///org/gnome/Shell/Extensions/js/extensions/prefs.js';

import GPasteGtk from 'gi://GPasteGtk?version=4';

export default class GPastePreferences extends ExtensionPreferences {
    getPreferencesWidget() {
        return new GPasteGtk.PreferencesWidget();
    }
}
