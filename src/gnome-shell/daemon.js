// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
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
    constructor(settings) {
        // Shared with the extension (and, through new_meta(), with the daemon,
        // its history and both clipboard providers): one settings instance per
        // process, as the C daemon does.
        this._settings = settings;
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

        this._nameLostId = this._bus.connect('name-lost', (_bus, wasOwned) => this._onNameLost(wasOwned));

        this._acquiredId = this._bus.connect('name-acquired', () => {
            this._onNameAcquired().catch(e => {
                console.error(e);
                this._resolveReady();
            });
        });

        this._bus.own_name_full(true);
    }

    _onNameLost(wasOwned) {
        if (wasOwned && this._bus?.is_connected()) {
            // Another GPaste daemon (typically a manually started
            // `gpaste-daemon --replace`) took the name over while the bus is
            // still alive. The user asked for that daemon, so flush, release the
            // lock and stand down for good rather than fighting back; the next
            // enable starts us over. Unowning also cancels our still-queued name
            // request: without that, GDBus silently re-acquires the name when the
            // other daemon exits, and we would then own it with nothing exported
            // (_onNameAcquired bails on _standDown), breaking every client.
            console.log('GPaste: the D-Bus name was taken over by another daemon, standing down');
            this._standDown = true;
            this._releaseDaemon();
        } else if (wasOwned) {
            // We held the name but the *connection* went away (a session-bus
            // restart), not a deliberate takeover: rebuild on a fresh connection
            // instead of staying dead until the extension is re-enabled.
            console.log('GPaste: lost the D-Bus connection, reconnecting');
            this._reconnect();
            return; // _reconnect() re-runs _start(), which resolves ready itself
        } else {
            console.error('GPaste: could not acquire the D-Bus name');
        }

        this._resolveReady();
    }

    // Tear down the dead bus and daemon, then re-own the name on a fresh
    // connection. g_bus_own_name() does not survive its connection closing, so a
    // transient session-bus outage needs an explicit re-request; a short delay
    // lets the bus come back first. A retry that still cannot acquire the name
    // lands in _onNameLost() with wasOwned === false, which simply gives up, so a
    // flapping bus can never spin this into a tight loop.
    _reconnect() {
        this._teardownBus();

        if (this._stopped)
            return;

        this._reconnectId = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 1, () => {
            this._reconnectId = 0;
            if (!this._stopped)
                this._start();
            return GLib.SOURCE_REMOVE;
        });
        GLib.Source.set_name_by_id(this._reconnectId, '[GPaste] bus reconnect');
    }

    _teardownBus() {
        if (this._nameLostId) {
            this._bus.disconnect(this._nameLostId);
            this._nameLostId = 0;
        }
        if (this._acquiredId) {
            this._bus.disconnect(this._acquiredId);
            this._acquiredId = 0;
        }

        this._releaseDaemon();
        this._bus = null;
    }

    async _onNameAcquired() {
        if (this._daemon || this._stopped)
            return;

        // The helpers below can block on a dialog for minutes, during which the
        // bus may be torn down and rebuilt (_reconnect) and this handler run
        // again. Pin the bus we were acquired on and drop out if it is no longer
        // the current one, so two concurrent runs cannot both build a daemon.
        const bus = this._bus;

        // The standalone daemon settles this in-process from its own main loop;
        // here we shell out to the dedicated helper for each concern (migration,
        // then decryption) and wait for it to settle the storage backend before
        // the daemon loads the history. Deciding what is needed stays here, in the
        // launcher (the same migration_needed/decryption_needed checks).
        if (!await this._settleStorage())
            return;

        if (this._daemon || bus !== this._bus)
            return;

        // global.display.get_selection() is mutter's per-display MetaSelection;
        // the same one backs both the clipboard and the primary provider.
        this._daemon = GPasteDaemon.Daemon.new_meta(this._settings, global.display.get_selection());
        this._searchProvider = GPasteDaemon.SearchProvider.new();

        // The daemon emits "reexecute-self" for a re-exec. gnome-shell cannot
        // reload an extension's code from disk on wayland, so a plain re-exec is
        // still a no-op here; but an on-demand storage migration (the client
        // resets the backend revision before asking for a re-exec) is handled in
        // place by reloading the daemon's storage — see _onReexecute().
        this._reexecId = this._daemon.connect('reexecute-self', () => this._onReexecute());

        bus.add_object(this._daemon);
        bus.add_object(this._searchProvider);

        this._resolveReady();
    }

    // Settle the storage backend (migration, then encrypted-history unlock) before
    // the daemon loads the history. The standalone daemon does this in-process from
    // its own main loop; here each concern is shelled out to the gpaste-storage
    // helper, which needs gtk/Adw that gnome-shell cannot provide. Deciding what is
    // needed stays here, in the launcher (the same needed() checks).
    //
    // Returns whether the caller may carry on (i.e. we were not stopped meanwhile).
    async _settleStorage() {
        // The gate keys are written by other processes (gpaste-client, the helper),
        // whose dconf notification may not have reached us yet; read them back
        // rather than trusting our cached copy.
        this._settings.reload();

        if (GPasteDaemon.storage_migration_needed(this._settings))
            await this._runStorageCommand('migrate');

        if (this._stopped)
            return false;

        // Re-checked after migration, since it may have (re)selected the encrypted
        // backend; also applies a usable keyring passphrase in this process, in
        // which case no helper is spawned.
        this._settings.reload();

        if (GPasteDaemon.storage_decryption_needed(this._settings))
            await this._runStorageCommand('decrypt');

        return !this._stopped;
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

    // "reexecute-self" fires synchronously from inside the daemon's Reexecute
    // D-Bus dispatch, which keeps using the daemon after we return; defer the
    // work to an idle so that dispatch fully unwinds first. Coalesce re-entry.
    _onReexecute() {
        if (this._reexecPending)
            return;

        this._reexecPending = true;
        GLib.Source.set_name_by_id(GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
            // Keep coalescing until the whole (async) migration has settled, so a
            // second re-exec arriving mid-migration cannot start a concurrent
            // _migrateInPlace() that reloads the store out from under the first.
            this._migrateInPlace()
                .catch(e => console.error(e))
                .finally(() => (this._reexecPending = false));
            return GLib.SOURCE_REMOVE;
        }), '[GPaste] reexecute-self');
    }

    async _migrateInPlace() {
        if (!this._daemon || this._stopped)
            return;

        // A plain re-exec has nothing to do here (we cannot reload our code); only
        // an on-demand migration, flagged by a reset backend revision, is actioned.
        // The client just wrote that revision, so read it back rather than waiting
        // on the dconf notification to reach our cached settings.
        this._settings.reload();
        if (!GPasteDaemon.storage_migration_needed(this._settings))
            return;

        // Flush before the helper reads the store so it imports the complete
        // history, then run the same helpers as startup (they show the dialog,
        // rewrite the store, bump the revision and hand back any passphrase)...
        const daemon = this._daemon;

        daemon.flush();

        if (!await this._settleStorage())
            return;

        // ...and reload the live daemon's backend from the now-migrated setting,
        // unless it was torn down or replaced while the helpers were running.
        if (daemon === this._daemon)
            daemon.reload_storage();
    }

    // Flush the history, release the storage lock, drop the daemon/search
    // provider and release the bus name — which, after a takeover, also
    // dequeues our pending re-request for it. Shared by an orderly shutdown
    // (extension disable) and standing down after a takeover.
    _releaseDaemon() {
        if (this._reexecId) {
            this._daemon?.disconnect(this._reexecId);
            this._reexecId = 0;
        }

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

        // A reconnect may be scheduled after a bus drop; drop it so it never
        // revives the daemon after teardown.
        if (this._reconnectId) {
            GLib.Source.remove(this._reconnectId);
            this._reconnectId = 0;
        }

        this._teardownBus();

        // The bus and the objects it held were already released by _teardownBus();
        // drop our reference to the extension's settings too.
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
