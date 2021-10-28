/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2021, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 * Based on js/misc/util.js from gnome-shell
 */

const Main = imports.ui.main;

const Gio = imports.gi.Gio;

/** */
function bypass() {
    let checker = Main.shellDBusService._senderChecker;
    if (!checker._gpasteEnabled) {
        checker._gpasteEnabled = true;
        checker._watchList.push(Gio.DBus.watch_name(Gio.BusType.SESSION,
                                                    'org.gnome.GPaste',
                                                    Gio.BusNameWatcherFlags.NONE,
                                                    (conn_, name_, owner) => checker._allowlistMap.set(name, owner),
                                                    () => checker._allowlistMap.delete(name)));
    }
}
