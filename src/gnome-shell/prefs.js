// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {ExtensionPreferences} from 'resource:///org/gnome/Shell/Extensions/js/extensions/prefs.js';

import GPasteGtk from 'gi://GPasteGtk?version=4';

export default class GPastePreferences extends ExtensionPreferences {
    // The pages, rather than the embeddable widget they are usually shown in.
    // That widget carries a view switcher of its own, and the default
    // getPreferencesWindow () path wraps whatever it is given in a preferences
    // group inside a page -- so GPaste's four pages and their switcher ended up
    // nested inside a single row of the extensions window's own preferences.
    // Handed the pages, that window switches between them itself, like every
    // other extension's.
    fillPreferencesWindow(window) {
        for (const page of GPasteGtk.preferences_pages_list())
            window.add(page);
    }
}
