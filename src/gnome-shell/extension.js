// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GPaste from 'gi://GPaste?version=3';

import {GPasteDaemonRunner} from './daemon.js';
import {GPasteIndicator} from './indicator.js';

export default class GPasteExtension extends Extension {
    enable() {
        this._settings = new GPaste.Settings();

        // The in-shell (mutter) daemon is experimental and opt-in: only host it
        // when "experimental-meta-daemon" is set. Otherwise the extension is a
        // pure client and the indicator's D-Bus connection activates the classic
        // standalone gpaste-daemon. The setting is read once here; changing it
        // takes effect the next time the extension is enabled.
        //
        // When we do host it, that in-process daemon must keep recording the
        // clipboard while the screen is locked, which is why the extension
        // declares the "unlock-dialog" session mode and stays enabled across the
        // lock. The panel indicator, on the other hand, must never be reachable
        // from the lock screen: it is created only in the normal "user" mode and
        // torn down whenever we leave it. Locking and unlocking therefore never
        // reach enable()/disable(); _sync() is what acts on the mode change.
        if (this._settings.get_experimental_meta_daemon())
            this._runner = new GPasteDaemonRunner(this._settings);

        this._sessionUpdatedId = Main.sessionMode.connect('updated', this._sync.bind(this));
        this._sync();
    }

    disable() {
        if (this._sessionUpdatedId) {
            Main.sessionMode.disconnect(this._sessionUpdatedId);
            this._sessionUpdatedId = 0;
        }

        // Hosting the daemon ourselves comes first: both routes below are D-Bus
        // calls dispatched a main loop turn later, by which time _runner's
        // shutdown() has released the name and dropped the daemon they were
        // meant for. Report to it directly instead, while it is still there.
        const reportedInProcess = !!this._runner?.reportExtensionGone();

        // Otherwise the indicator is what reports our state to the daemon, but
        // it cannot always: outside the "user" session mode there is none (a
        // disable reached from a locked session — an extension update, or
        // gnome-extensions disable), and one that never reached a daemon has no
        // client to report through. It tells us which happened, and we report it
        // ourselves when it could not. Exactly one of the two reports, whichever
        // way it went.
        if (!this._destroyIndicator(true) && !reportedInProcess)
            this._reportExtensionGone();

        this._runner?.shutdown();
        this._runner = null;
        this._settings = null;
    }

    // Tell the daemon the extension went away, without a GPasteClient and
    // without touching its settings.
    //
    // Whether that should stop the clipboard from being tracked is the daemon's
    // call, not ours: "track-extension-state" means "stop tracking when the
    // extension does", and ReportExtensionState is where it is applied.
    // Writing "track-changes" from here would be us second-guessing a key the
    // daemon owns, on a policy it already implements.
    //
    // The call goes straight to the bus rather than through a GPasteClient,
    // whose proxy is built with G_DBUS_PROXY_FLAGS_NONE: DO_NOT_AUTO_START is
    // the whole point here, since being disabled must never be what *starts* a
    // daemon. With none running there is nothing tracking the clipboard and so
    // nothing to report to, which is exactly what the failed call means.
    _reportExtensionGone() {
        Gio.DBus.session.call(
            'org.gnome.GPaste',
            '/org/gnome/GPaste',
            'org.gnome.GPaste3',
            'ReportExtensionState',
            new GLib.Variant('(b)', [false]),
            null,
            Gio.DBusCallFlags.DO_NOT_AUTO_START,
            -1,
            null,
            (bus, res) => {
                try {
                    bus.call_finish(res);
                } catch (e) {
                    if (!e.matches(Gio.DBusError, Gio.DBusError.SERVICE_UNKNOWN))
                        console.error(`GPaste: ${e.message}`);
                }
            });
    }

    _sync() {
        if (Main.sessionMode.currentMode === 'user')
            this._createIndicator();
        else
            this._destroyIndicator(false);
    }

    _createIndicator() {
        if (this._indicatorWanted)
            return;
        this._indicatorWanted = true;

        // With the in-shell daemon, only build the panel UI once it owns the bus
        // name, so a fresh start never triggers D-Bus activation of the
        // standalone daemon. Without it, there is nothing to wait for and adding
        // the indicator is exactly what activates the standalone daemon.
        if (!this._runner) {
            this._addIndicator();
            return;
        }

        this._runner.ready.then(() => this._addIndicator()).catch(console.error);
    }

    _addIndicator() {
        // The mode (or the whole extension) may change again before the runner is
        // ready, so re-check before actually adding the indicator.
        if (!this._indicatorWanted || Main.panel.statusArea.gpaste)
            return;
        Main.panel.addToStatusArea('gpaste', new GPasteIndicator());
    }

    // @extensionDisabled distinguishes the extension going away from the
    // indicator being dropped because we left the "user" session mode (the lock
    // screen): only the former is a state change the daemon should hear about.
    //
    // Returns whether that state change was reported to the daemon — %false both
    // when there was nothing to report and when there was no indicator (or no
    // client) to report it.
    _destroyIndicator(extensionDisabled) {
        this._indicatorWanted = false;
        // shutdown() destroys the actor, which removes it from the panel's
        // statusArea; nothing to do when there is no indicator.
        return !!Main.panel.statusArea.gpaste?.shutdown(extensionDisabled);
    }
}
