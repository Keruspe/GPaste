/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *	Copyright 2011 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *	This file is part of GPaste.
 *
 *	GPaste is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	GPaste is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

namespace GPaste {

    namespace Daemon {

        [DBus (name = "org.gnome.GPaste")]
        public class DBusServer : GLib.Object {
            [DBus (name = "GetHistory", inSignature = "", outSignature = "as")]
            public GLib.Variant getHistory() {
                stdout.printf("%s\n", this.active ? "True" : "False");
                unowned GLib.SList<string> history = History.instance.history;
                var vb = new GLib.VariantBuilder(new GLib.VariantType.array(GLib.VariantType.STRING));
                foreach (string s in history)
                    vb.add_value(s);
                return vb.end();
            }

            [DBus (name = "Add", inSignature = "s", outSignature = "")]
            public void add(string selection) {
                ClipboardsManager.instance.select(selection);
                History.instance.add(selection);
            }

            [DBus (name = "Select", inSignature = "u", outSignature = "")]
            public void select(uint32 index) {
                History.instance.select(index);
            }

            [DBus (name = "Delete", inSignature = "u", outSignature = "")]
            public void delete(uint32 index) {
                History.instance.delete(index);
            }

            [DBus (name = "Empty", inSignature = "", outSignature = "")]
            public void empty() {
                History.instance.empty();
            }

            [DBus (name = "Launch", inSignature = "", outSignature = "")]
            public void launch() {
                this.active = true;
                this.start();
            }

            [DBus (name = "Quit", inSignature = "", outSignature = "")]
            public void quit() {
                this.active = false;
                this.exit();
            }

            [DBus (name = "Start", inSignature = "", outSignature = "")]
            public signal void start();

            [DBus (name = "Changed", inSignature = "", outSignature = "")]
            public signal void changed();

            [DBus (name = "Exit", inSignature = "", outSignature = "")]
            public signal void exit();

            [DBus (name = "Active", signature = "b", access = "readonly")]
            public bool active { get; private set; }

            private DBusServer() {
                this.active = true;
            }

            private static DBusServer _instance;
            public static DBusServer instance {
                get {
                    if (DBusServer._instance == null)
                        DBusServer._instance = new DBusServer();
                    return DBusServer._instance;
                }
            }
        }

        public class Main : GLib.Object {
            public static GLib.MainLoop loop { get; private set; }

            private static void handle(int signal) {
                stdout.printf(_("Signal %d recieved, exiting.\n"), signal);
                DBusServer.instance.exit();
                Main.loop.quit();
            }

            private static void on_bus_aquired(DBusConnection conn) {
                try {
                    conn.register_object("/org/gnome/GPaste", DBusServer.instance);
                } catch (IOError e) {
                    stderr.printf(_("Could not register DBus service.\n"));
                }
            }

            private static void start_dbus() {
                Bus.own_name(BusType.SESSION, "org.gnome.GPaste", BusNameOwnerFlags.NONE,
                    Main.on_bus_aquired, () => {}, () => {
                        stderr.printf(_("Could not aquire DBus name.\n"));
                        Posix.exit(1);
                    }
                );
            }

            public static int main(string[] args) {
                GLib.Intl.bindtextdomain(Config.GETTEXT_PACKAGE, Config.LOCALEDIR);
                GLib.Intl.bind_textdomain_codeset(Config.GETTEXT_PACKAGE, "UTF-8");
                GLib.Intl.textdomain(Config.GETTEXT_PACKAGE);
                Gtk.init(ref args);
                History.instance.load();
                var clipboard = new Clipboard(Gdk.SELECTION_CLIPBOARD);
                var primary = new Clipboard(Gdk.SELECTION_PRIMARY);
                var cm = ClipboardsManager.instance;
                cm.addClipboard(clipboard);
                cm.addClipboard(primary);
                cm.activate();
                var handler = Posix.sigaction_t();
                handler.sa_handler = handle;
                Posix.sigaction(Posix.SIGTERM, handler, null);
                Posix.sigaction(Posix.SIGINT, handler, null);
                Main.start_dbus();
                Main.loop = new GLib.MainLoop(null, false);
                Main.loop.run();
                return 0;
            }
        }

    }

}
