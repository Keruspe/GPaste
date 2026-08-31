// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import GLib from 'gi://GLib';
import GPasteDaemon from 'gi://GPasteDaemon?version=1';

import {GPasteShellPrompt} from './prompt.js';

// The three storage concerns are ordinary GAsyncResult operations, but they are
// static functions on the namespace rather than methods on a prototype, so
// Gio._promisify is not usable on them: the wrapper assignment is subject to the
// same silent no-op as a static constructor inside gnome-shell, and its body
// starts by reading `this[originalFuncName]`, which in a strict-mode ES module
// is a TypeError rather than a call. Both failures are quiet — the concern
// simply never runs — so promise-wrap the raw _async/_finish pair by hand, the
// way indicator.js does for GPaste.Client.new().
function callStorageConcern(concernAsync, concernFinish, prompt, settings) {
    return new Promise((resolve, reject) => {
        concernAsync(prompt, settings, (_source, result) => {
            try {
                concernFinish(result);
                resolve();
            } catch (e) {
                reject(e);
            }
        });
    });
}

// Runs the GPaste daemon inside gnome-shell, mirroring src/daemon/gpaste-daemon.c
// but driving the mutter (MetaSelection) clipboard backend so the clipboard can
// be watched from within the compositor itself. Compared to the standalone
// daemon there is no GApplication, no POSIX signal handling and no re-exec, and
// the storage-backend choice / migration dialog and the encrypted-history unlock
// put their questions to the user through a GPasteShellPrompt (St dialogs), the
// gnome-shell counterpart of the standalone daemon's libadwaita prompt backend:
// the storage code itself is the same, and runs in this process.
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
        this._prompt = new GPasteShellPrompt();
        this._standDown = false;
        this._shutDown = false;

        // The deferred jobs (_deferOnce) currently in flight: signal name to the
        // id of the idle still waiting to run it, 0 once the job has started.
        this._pending = new Map();

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
        return this._standDown || this._shutDown;
    }

    _start() {
        this._bus = GPasteDaemon.Bus.new();

        this._bus.connectObject(
            'export-failed', () => this._onExportFailed(),
            'name-lost', (_bus, wasOwned) => this._onNameLost(wasOwned),
            'name-acquired', () => {
                this._onNameAcquired().catch(e => {
                    console.error(e);
                    this._resolveReady();
                });
            },
            this);

        this._bus.own_name_full(true);
    }

    // Owning the name is not enough: an object that failed to export leaves every
    // client call landing on a path that does not exist, and our holding the name
    // is precisely what stops D-Bus from activating the standalone daemon that
    // would work. Stand down so it can, mirroring the standalone daemon treating
    // this as a startup failure and exiting. (The bus warned about the cause.)
    _onExportFailed() {
        console.error('GPaste: could not export our objects on the session bus, standing down');
        this._standDown = true;
        this._releaseDaemon();
        this._resolveReady();
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

    // The bus is not an actor, so nothing disconnects us automatically: do it by
    // hand before dropping it. Safe to run twice (a reconnect that is shut down
    // before it fires leaves no bus behind).
    _teardownBus() {
        this._bus?.disconnectObject(this);

        this._releaseDaemon();
        this._bus = null;
    }

    async _onNameAcquired() {
        if (this._daemon || this._stopped)
            return;

        // Settling the storage below can block on a dialog for minutes, during
        // which the bus may be torn down and rebuilt (_reconnect) and this
        // handler run again. Pin the bus we were acquired on and drop out if it
        // is no longer the current one, so two concurrent runs cannot both build
        // a daemon.
        const bus = this._bus;

        // Settle the storage backend before the daemon loads the history, exactly
        // as the standalone daemon does from its own main loop. Deciding what is
        // needed stays here, in the launcher.
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
        // place by reloading the daemon's storage — see _migrateInPlace(). Same
        // deal for a passphrase change — see _changePassphraseInPlace().
        this._daemon.connectObject(
            'reexecute-self', () => this._deferOnce('reexecute-self', () => this._migrateInPlace()),
            'change-passphrase', () => this._deferOnce('change-passphrase', () => this._changePassphraseInPlace()),
            this);

        bus.add_object(this._daemon);

        // Exporting can fail, which stands us down and drops the daemon (see
        // _onExportFailed, which runs from inside add_object and resolves ready
        // itself): there is nothing left to export then.
        if (this._stopped)
            return;

        bus.add_object(this._searchProvider);

        this._resolveReady();
    }

    // Settle the storage backend (migration, then encrypted-history unlock) before
    // the daemon loads the history, the same way the standalone daemon does from
    // its own main loop. Deciding what is needed stays here, in the launcher (the
    // same needed() checks).
    //
    // Returns whether the caller may carry on (i.e. we were not stopped meanwhile).
    //
    // Only ever one settle in flight: a prompt can sit there for minutes, and a
    // bus reconnect during that window runs _onNameAcquired() again — which would
    // raise a second set of dialogs over the very store the first is rewriting. A
    // later caller joins the running settle instead; its answer ("may I carry
    // on") is the same for both, since it only reports whether we were stopped
    // meanwhile.
    _settleStorage() {
        // Joining is specific to the settle: two settles ask the same question
        // and one answer serves both. Every other concern gets its own turn
        // through _serializeStorage() instead — see there.
        if (!this._settling) {
            this._settling = this._serializeStorage(() => this._doSettleStorage())
                .finally(() => {
                    this._settling = null;
                });
        }

        return this._settling;
    }

    // Only one storage concern may touch the store at a time, whoever asked for
    // it: without this a `gpaste-client change-passphrase` arriving while a bus
    // reconnect is re-settling would have the re-key re-encrypt every history
    // while the settle is still unlocking and loading it, with two modal
    // dialogs stacked over each other.
    //
    // They therefore run one after another — chained, not dropped. Handing a
    // later caller the promise of the concern already running would answer it
    // with something it never asked for: a re-key would silently never happen
    // (no dialog, no error, and reload_storage() would then reload the very
    // passphrase it was told to replace), and a settle joined onto a re-key
    // would read that concern's `undefined` as "we were stopped" and stand the
    // runner down with the bus name owned and no daemon behind it.
    //
    // A concern that comes up for its turn after teardown is dropped rather
    // than run: the prompt would refuse it anyway, but the storage layer reads
    // (and, encrypted, derives a key from) the store before it ever asks.
    _serializeStorage(concern) {
        const mine = (this._storageChain ?? Promise.resolve())
            .catch(() => {}) // one concern failing must not skip the next
            .then(() => this._stopped ? undefined : concern());

        // The tail is only there to order what comes next, so it must never
        // reject; each caller still sees its own outcome through @mine.
        this._storageChain = mine.catch(() => {});

        return mine;
    }

    async _doSettleStorage() {
        // The gate keys are written by another process (gpaste-client, opening
        // the migration gate before asking us to re-execute), whose dconf
        // notification may not have reached us yet; read them back rather than
        // trusting our cached copy.
        this._settings.reload();

        if (GPasteDaemon.storage_migration_needed(this._settings)) {
            await this._runStorageConcern(GPasteDaemon.storage_migration_async,
                GPasteDaemon.storage_migration_finish);
        }

        if (this._stopped)
            return false;

        // Re-checked after migration, since it may have (re)selected the encrypted
        // backend; also applies a usable keyring passphrase in this process, in
        // which case there is nothing left to ask.
        this._settings.reload();

        if (GPasteDaemon.storage_decryption_needed(this._settings)) {
            await this._runStorageConcern(GPasteDaemon.storage_decryption_async,
                GPasteDaemon.storage_decryption_finish);
        }

        return !this._stopped;
    }

    // Whatever a concern settles on is set process-wide by the storage code
    // itself — we are the process now, so nothing has to be handed back and the
    // result is only ever "did it fail". The prompt here is ours
    // (GPasteShellPrompt), so a rejection from one of the two prompt-only
    // concerns means a bug on our side rather than anything the user did; the
    // re-key can also report that it could not rewrite the histories on disk.
    // The concern is over either way: log it and carry on to the next one.
    async _runStorageConcern(concernAsync, concernFinish) {
        try {
            await callStorageConcern(concernAsync, concernFinish, this._prompt, this._settings);
        } catch (e) {
            logError(e, 'GPaste: storage concern failed');
        }
    }

    // Both signals fire synchronously from inside the daemon's own D-Bus
    // dispatch (Reexecute, ChangePassphrase), which keeps using the daemon after
    // we return: defer @job to an idle so that dispatch fully unwinds first.
    //
    // Keep coalescing under @name until the whole (async) job has settled, not
    // merely until the idle runs, so a second request arriving mid-job cannot
    // start a concurrent one that reloads the store out from under the first.
    _deferOnce(name, job) {
        if (this._pending.has(name))
            return;

        const id = GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
            this._pending.set(name, 0);
            // Promise.resolve() so that a job returning early (or throwing)
            // rather than a promise still clears @name: leaving it behind would
            // coalesce away every later request for the rest of the session.
            Promise.resolve().then(job)
                .catch(e => console.error(e))
                .finally(() => this._pending.delete(name));
            return GLib.SOURCE_REMOVE;
        });

        this._pending.set(name, id);
        GLib.Source.set_name_by_id(id, `[GPaste] ${name}`);
    }

    // Run a storage concern against the live daemon and rebuild its backend
    // around whatever the concern settled on. Shared by the on-demand migration
    // and the re-key, which differ only in the concern they run.
    //
    // Flushing happens inside our own turn on the chain, together with the
    // concern: the flush has to cover our complete state before the store is
    // read or rewritten, but doing it before queueing would stop the history
    // from recording (and make every Delete over the bus fail with BUSY) for
    // however long an unrelated concern ahead of us keeps its dialog up. Our own
    // turn — rather than _settleStorage() — is also what stops us from joining a
    // settle that has already read past our flush.
    async _reloadStorageAfter(concern) {
        if (!this._daemon || this._stopped)
            return;

        const daemon = this._daemon;

        await this._serializeStorage(() => {
            daemon.flush();
            return concern();
        });

        if (this._stopped)
            return;

        // Reload the live daemon's backend, unless it was torn down or replaced
        // while the helpers were running.
        if (daemon === this._daemon)
            daemon.reload_storage();
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

        // The same concern as startup: it shows the dialog, rewrites the store
        // and bumps the revision.
        await this._reloadStorageAfter(() => this._doSettleStorage());
    }

    _changePassphraseInPlace() {
        return this._reloadStorageAfter(() => this._runStorageConcern(GPasteDaemon.storage_rekey_async,
            GPasteDaemon.storage_rekey_finish));
    }

    // Flush the history, release the storage lock, drop the daemon/search
    // provider and release the bus name — which, after a takeover, also
    // dequeues our pending re-request for it. Shared by an orderly shutdown
    // (extension disable) and standing down after a takeover.
    _releaseDaemon() {
        this._daemon?.disconnectObject(this);

        // Before the flush, so the selection this changes is recorded with the
        // rest of it: a password still on a selection has to come off while
        // there is a daemon to take it off with. Dropping our reference below
        // only hands the wrapper to the garbage collector, and mutter goes on
        // serving the source we published until the manager behind it is
        // actually disposed -- which for a password is its cleartext, on the
        // clipboard, for the rest of the session. The standalone daemon's
        // re-exec does the same thing for the same reason; its plain exit needs
        // nothing, the selection dying with the process.
        this._daemon?.expire_password();

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

    // Tell our own daemon the extension went away, in process. The D-Bus call the
    // extension makes otherwise cannot work here: it is dispatched a main loop
    // turn later, and shutdown() releases the name (and drops the daemon) right
    // after — so the "track-extension-state" policy would silently never be
    // applied on the one setup where we are the daemon.
    //
    // Returns whether there was a daemon of ours to tell.
    reportExtensionGone() {
        if (!this._daemon)
            return false;

        this._daemon.extension_state_changed(false);
        return true;
    }

    shutdown() {
        this._shutDown = true;

        // Abandon any storage prompt still on screen: with the extension going
        // away there is nobody left to act on the answer, and a request nobody
        // answers leaves _settleStorage() pending for good.
        this._prompt.shutdown();

        // A reconnect may be scheduled after a bus drop; drop it so it never
        // revives the daemon after teardown.
        if (this._reconnectId) {
            GLib.Source.remove(this._reconnectId);
            this._reconnectId = 0;
        }

        // Same for a deferred job whose idle has not run yet: the jobs bail out
        // on _stopped anyway, but there is no reason to keep the extension's
        // settings and prompt alive for another main loop turn.
        for (const id of this._pending.values()) {
            if (id)
                GLib.Source.remove(id);
        }
        this._pending.clear();

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
