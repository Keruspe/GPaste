// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import Gio from 'gi://Gio';
import GPaste from 'gi://GPaste?version=2';
import GPasteDaemon from 'gi://GPasteDaemon?version=1';

Gio._promisify(Gio.Subprocess.prototype, 'communicate_utf8_async', 'communicate_utf8_finish');

// Runs the GPaste daemon inside gnome-shell, mirroring src/daemon/gpaste-daemon.c
// but driving the mutter (MetaSelection) clipboard backend so the clipboard can
// be watched from within the compositor itself. Compared to the standalone
// daemon there is no GApplication, no POSIX signal handling and no re-exec, and
// the storage-backend choice / migration dialog and the encrypted-history unlock
// are not run in-process (they need gtk_init/Adw, which gnome-shell does not
// provide): instead the gpaste-storage helper is spawned for each concern.
export class GPasteDaemonRunner {
    constructor() {
        this._settings = GPaste.Settings.new();
        this._cancellable = new Gio.Cancellable();

        this._start().catch(console.error);
    }

    async _start() {
        // The standalone daemon settles this in-process from its own main loop;
        // here we shell out to the dedicated helper for each concern (migration,
        // then decryption) and wait for it to settle the storage backend before
        // the daemon loads the history. Deciding what is needed stays here, in the
        // launcher (the same migration_needed/decryption_needed checks).
        if (GPasteDaemon.storage_migration_needed(this._settings))
            await this._runStorageCommand('migrate');

        if (this._cancellable.is_cancelled())
            return;

        // Re-checked after migration, since it may have (re)selected the encrypted
        // backend; also applies a usable keyring passphrase in this process, in
        // which case no helper is spawned.
        if (GPasteDaemon.storage_decryption_needed(this._settings))
            await this._runStorageCommand('decrypt');

        if (this._cancellable.is_cancelled())
            return;

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

    async _runStorageCommand(command) {
        try {
            const proc = GPaste.util_spawn_storage(command);
            // The helper writes back the encrypted-history passphrase it ended up
            // with (it runs in its own process and cannot share the process-wide
            // global), so set it here before the daemon loads the history.
            const [, passphrase] = await proc.communicate_utf8_async(null, this._cancellable);
            if (passphrase)
                GPasteDaemon.storage_backend_set_passphrase(passphrase);
        } catch (e) {
            console.error(`GPaste: storage ${command} helper failed: ${e.message}`);
        }
    }

    shutdown() {
        this._cancellable.cancel();

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

        // Wipe the master passphrase from gcr secure memory on teardown, the way
        // the standalone daemon does before exiting.
        //
        // storage_backend_set_passphrase is compiled only with encryption support
        // (G_PASTE_ENABLE_ENCRYPTION), so its binding is missing from builds without
        // it; calling it unconditionally would throw on the undefined member, hence
        // the existence check.
        if (GPasteDaemon.storage_backend_set_passphrase)
            GPasteDaemon.storage_backend_set_passphrase(null);
    }
}
