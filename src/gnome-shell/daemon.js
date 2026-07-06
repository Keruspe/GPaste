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
//
// Like the standalone daemon it owns the name first and only builds the daemon
// once it is acquired, but it always requests replacement: hosting the daemon in
// gnome-shell is the future-proof path (GTK will drop its X11 backend), so we
// evict any standalone daemon that happens to hold the name.
export class GPasteDaemonRunner {
    constructor() {
        this._settings = GPaste.Settings.new();
        this._cancellable = new Gio.Cancellable();
        this._standDown = false;

        // Resolves once the daemon has been built and registered on the bus, or
        // the runner has given up; the indicator awaits it before connecting so
        // the common path never activates the standalone daemon by accident.
        this.ready = new Promise(resolve => {
            this._resolveReady = resolve;
        });

        this._start();
    }

    // Everything the runner does asynchronously has to bail once it has been shut
    // down (the extension is being disabled) or has stood down after a takeover.
    get _stopped() {
        return this._standDown || this._cancellable.is_cancelled();
    }

    _start() {
        this._bus = GPasteDaemon.Bus.new();

        this._nameLostId = this._bus.connect('name-lost', (_bus, wasOwned) => {
            if (wasOwned) {
                // Another GPaste daemon (typically a manually started
                // `gpaste-daemon --replace`) took the name over. The user asked
                // for that daemon, so flush, release the lock and stand down for
                // good rather than fighting back; the next enable starts us over.
                // Unowning also cancels our still-queued name request: without
                // that, GDBus silently re-acquires the name when the other
                // daemon exits, and we would then own it with nothing exported
                // (_onNameAcquired bails on _standDown), breaking every client.
                console.log('GPaste: the D-Bus name was taken over by another daemon, standing down');
                this._standDown = true;
                this._releaseDaemon();
            } else {
                console.error('GPaste: could not acquire the D-Bus name');
            }
            this._resolveReady();
        });

        this._acquiredId = this._bus.connect('name-acquired', () => {
            this._onNameAcquired().catch(e => {
                console.error(e);
                this._resolveReady();
            });
        });

        this._bus.own_name_full(true);
    }

    async _onNameAcquired() {
        if (this._daemon || this._standDown || this._cancellable.is_cancelled())
            return;

        // The standalone daemon settles this in-process from its own main loop;
        // here we shell out to the dedicated helper for each concern (migration,
        // then decryption) and wait for it to settle the storage backend before
        // the daemon loads the history. Deciding what is needed stays here, in the
        // launcher (the same migration_needed/decryption_needed checks).
        if (GPasteDaemon.storage_migration_needed(this._settings))
            await this._runStorageCommand('migrate');

        if (this._stopped)
            return;

        // Re-checked after migration, since it may have (re)selected the encrypted
        // backend; also applies a usable keyring passphrase in this process, in
        // which case no helper is spawned.
        if (GPasteDaemon.storage_decryption_needed(this._settings))
            await this._runStorageCommand('decrypt');

        if (this._stopped)
            return;

        // global.display.get_selection() is mutter's per-display MetaSelection;
        // the same one backs both the clipboard and the primary provider.
        this._daemon = GPasteDaemon.Daemon.new_meta(this._settings, global.display.get_selection());
        this._searchProvider = GPasteDaemon.SearchProvider.new();

        // The daemon emits "reexecute-self" for a re-exec, which is meaningless
        // here: gnome-shell cannot reload an extension's code from disk on
        // wayland, so we deliberately leave the signal unhandled (a no-op).
        this._bus.add_object(this._daemon);
        this._bus.add_object(this._searchProvider);

        this._resolveReady();
    }

    async _runStorageCommand(command) {
        try {
            const proc = GPaste.util_spawn_storage(command);
            // The helper writes back the encrypted-history passphrase it ended up
            // with (it runs in its own process and cannot share the process-wide
            // global), so set it here before the daemon loads the history.
            const [, passphrase] = await proc.communicate_utf8_async(null, this._cancellable);
            if (passphrase)
                GPasteDaemon.StorageBackend.set_passphrase(passphrase);
        } catch (e) {
            console.error(`GPaste: storage ${command} helper failed: ${e.message}`);
        }
    }

    // Flush the history, release the storage lock, drop the daemon/search
    // provider and release the bus name — which, after a takeover, also
    // dequeues our pending re-request for it. Shared by an orderly shutdown
    // (extension disable) and standing down after a takeover.
    _releaseDaemon() {
        // Flush before releasing the lock so a successor daemon loads our final
        // state; drain happens synchronously inside flush().
        this._daemon?.flush();

        // own_name_full() makes GDBus' name-owner closure hold a ref on the bus,
        // so merely dropping our reference would never release the name: that ref
        // is only dropped once the name is unowned. Do it explicitly so the name
        // is released synchronously instead of lingering. It also unexports the
        // daemon and the search provider, whose registrations would otherwise keep
        // them alive — and their object paths taken — on gnome-shell's shared
        // session-bus connection, making the next enable() fail to register.
        this._bus?.unown_name();

        GPasteDaemon.StorageBackend.unlock();

        this._daemon = null;
        this._searchProvider = null;
    }

    shutdown() {
        this._cancellable.cancel();

        if (this._nameLostId) {
            this._bus.disconnect(this._nameLostId);
            this._nameLostId = 0;
        }
        if (this._acquiredId) {
            this._bus.disconnect(this._acquiredId);
            this._acquiredId = 0;
        }

        this._releaseDaemon();

        // The bus (and the objects it held) plus the shared settings go away once
        // we drop our references too.
        this._bus = null;
        this._settings = null;

        // Wipe the master passphrase from gcr secure memory on teardown, the way
        // the standalone daemon does before exiting.
        //
        // StorageBackend.set_passphrase is compiled only with encryption support
        // (G_PASTE_ENABLE_ENCRYPTION), so its binding is missing from builds without
        // it; calling it unconditionally would throw on the undefined member, hence
        // the existence check.
        if (GPasteDaemon.StorageBackend.set_passphrase)
            GPasteDaemon.StorageBackend.set_passphrase(null);
    }
}
