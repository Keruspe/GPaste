// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import GPaste from 'gi://GPaste?version=2';
import GPasteDaemon from 'gi://GPasteDaemon?version=1';

// Runs the GPaste daemon inside gnome-shell, mirroring src/daemon/gpaste-daemon.c
// but driving the mutter (MetaSelection) clipboard backend so the clipboard can
// be watched from within the compositor itself. Compared to the standalone
// daemon there is no GApplication, no POSIX signal handling, no re-exec and no
// storage-migration dialog: this object's lifetime is the extension's, and the
// migration UI stays with the standalone daemon / preferences.
export class GPasteDaemonRunner {
    constructor() {
        this._settings = GPaste.Settings.new();

        // global.display.get_selection() is mutter's per-display MetaSelection;
        // the same one backs both the clipboard and the primary provider.
        this._daemon = GPasteDaemon.Daemon.new_meta(this._settings, global.display.get_selection());
        this._searchProvider = GPasteDaemon.SearchProvider.new();

        this._bus = GPasteDaemon.Bus.new();
        this._bus.add_object(this._daemon);
        this._bus.add_object(this._searchProvider);

        this._nameLostId = this._bus.connect('name-lost', () => {
            console.error('GPaste: could not acquire the D-Bus name');
        });

        this._bus.own_name();
    }

    shutdown() {
        if (this._nameLostId) {
            this._bus.disconnect(this._nameLostId);
            this._nameLostId = 0;
        }

        // own_name() makes GDBus' name-owner closure hold a ref on the bus, so
        // merely dropping our reference would never release the name: that ref
        // is only dropped once the name is unowned. Do it explicitly so the name
        // is released synchronously on disable instead of lingering forever.
        this._bus?.unown_name();

        // The daemon and the search provider the bus held, plus the shared
        // settings, go away once we drop our references too.
        this._bus = null;
        this._daemon = null;
        this._searchProvider = null;
        this._settings = null;
    }
}
