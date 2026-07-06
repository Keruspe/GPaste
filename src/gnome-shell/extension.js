// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

import {GPasteDaemonRunner} from './daemon.js';
import {GPasteIndicator} from './indicator.js';

export default class GPasteExtension extends Extension {
    enable() {
        // The GPaste daemon runs in-process (see daemon.js) and must keep
        // recording the clipboard while the screen is locked, so the extension
        // declares the "unlock-dialog" session mode and stays enabled across the
        // lock. The panel indicator, on the other hand, must never be reachable
        // from the lock screen: it is created only in the normal "user" mode and
        // torn down whenever we leave it. enable()/disable() are therefore no
        // longer called on lock/unlock; the mode change is handled by _sync().
        this._runner = new GPasteDaemonRunner();

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

        // Only build the panel UI once the in-process daemon is up, so a fresh
        // start never triggers D-Bus activation of the standalone daemon. The
        // mode (or the whole extension) may change again before it is ready, so
        // re-check before actually adding the indicator.
        this._runner.ready.then(() => {
            if (!this._indicatorWanted || Main.panel.statusArea.gpaste)
                return;
            Main.panel.addToStatusArea('gpaste', new GPasteIndicator());
        }).catch(console.error);
    }

    _destroyIndicator() {
        this._indicatorWanted = false;
        // shutdown() destroys the actor, which removes it from the panel's
        // statusArea; nothing to do when there is no indicator.
        Main.panel.statusArea.gpaste?.shutdown();
    }
}
