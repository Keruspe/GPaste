/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2023, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import { ExtensionPreferences, gettext as _ } from 'resource:///org/gnome/Shell/Extensions/js/extensions/prefs.js';

import GPasteGtk from 'gi://GPasteGtk?version=4';

export default class GPastePreferences extends ExtensionPreferences {
    getPreferencesWidget() {
        return new GPasteGtk.PreferencesWidget();
    }
}
