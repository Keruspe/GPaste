// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

import GPaste from 'gi://GPaste?version=2';

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
        // torn down whenever we leave it. enable()/disable() are therefore no
        // longer called on lock/unlock; the mode change is handled by _sync().
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

        this._destroyIndicator();

        this._runner?.shutdown();
        this._runner = null;
        this._settings = null;
    }

    _sync() {
        if (Main.sessionMode.currentMode === 'user')
            this._createIndicator();
        else
            this._destroyIndicator();
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

    _destroyIndicator() {
        this._indicatorWanted = false;
        // shutdown() destroys the actor, which removes it from the panel's
        // statusArea; nothing to do when there is no indicator.
        Main.panel.statusArea.gpaste?.shutdown();
    }
}
