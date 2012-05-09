/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2012 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

namespace GPaste {

    [DBus (name = "org.gnome.GPaste")]
    public interface DBusClient : GLib.Object {
        [DBus (name = "GetHistory", inSignature = "", outSignature = "as")]
        public abstract string[] get_history() throws GLib.IOError;
        [DBus (name = "BackupHistory", inSignature = "s", outSignature = "")]
        public abstract void backup_history(string name) throws GLib.IOError;
        [DBus (name = "SwitchHistory", inSignature = "s", outSignature = "")]
        public abstract void switch_history(string name) throws GLib.IOError;
        [DBus (name = "DeleteHistory", inSignature = "s", outSignature = "")]
        public abstract void delete_history(string name) throws GLib.IOError;
        [DBus (name = "ListHistories", inSignature = "", outSignature = "as")]
        public abstract string[] list_histories() throws GLib.IOError;
        [DBus (name = "Add", inSignature = "s", outSignature = "")]
        public abstract void add(string selection) throws GLib.IOError;
        [DBus (name = "Select", inSignature = "u", outSignature = "")]
        public abstract void select(uint32 pos) throws GLib.IOError;
        [DBus (name = "Delete", inSignature = "u", outSignature = "")]
        public abstract void delete(uint32 pos) throws GLib.IOError;
        [DBus (name = "Empty", inSignature = "", outSignature = "")]
        public abstract void empty() throws GLib.IOError;
        [DBus (name = "Track", inSignature = "b", outSignature = "")]
        public abstract void track(bool tracking_state) throws GLib.IOError;
        [DBus (name = "Reexecute", inSignature = "", outSignature = "")]
        public abstract void reexec() throws GLib.IOError;
        [DBus (name = "Changed", inSignature = "")]
        public abstract signal void changed ();
        [DBus (name = "ShowHistory", inSignature = "")]
        public abstract signal void show_history ();
    }

}
