/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 * Based on js/misc/util.js from gnome-shell
 */

import * as Main from 'resource:///org/gnome/shell/ui/main.js';

import GLib from 'gi://GLib?version=2.0';
import Gio from 'gi://Gio?version=2.0';

// Sadly, there is no other way of making global keybindings work on wayland until we get a portal for this.
// I really wish we could avoid this, and the fact that we do this defeats the whole purpose of the added security from
// gnome-shell which is... really sad.
export default function checkerBypass() {
    if (!Main.shellDBusService) {
        // we got loaded too early, the dbus service isn't ready so we cannot hook ourselves in, try back later
        GLib.Source.set_name_by_id(GLib.idle_add_once(checkerBypass), '[GPaste] checkerBypass');
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
};
