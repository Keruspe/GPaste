/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2021, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 * Based on js/misc/util.js from gnome-shell
 */

const Main = imports.ui.main;

const { GLib, Gio } = imports.gi;

// Sadly, there is no other way of making global keybindings work on wayland until we get a portal for this.
// I really wish we could avoid this, and the fact that we do this defeats the whole purpose of the added security from
// gnome-shell which is... really sad.

/** */
function bypass() {
    if (!Main.shellDBusService) {
        // we got loaded too early, the dbus service isn't ready so we cannot hook ourselves in, try back later
        GLib.idle_add(GLib.PRIORITY_DEFAULT, bypass);
        return;
    }
    let checker = Main.shellDBusService._senderChecker;
    if (!checker._gpasteEnabled) {
        checker._gpasteEnabled = true;
        checker._watchList.push(Gio.DBus.watch_name(Gio.BusType.SESSION,
                                                    'org.gnome.GPaste',
                                                    Gio.BusNameWatcherFlags.NONE,
                                                    (conn_, name_, owner) => checker._allowlistMap.set('org.gnome.GPaste', owner),
                                                    () => checker._allowlistMap.delete('org.gnome.GPaste')));
    }
}
