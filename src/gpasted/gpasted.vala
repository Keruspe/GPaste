/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

    namespace Daemon {

        [DBus (name = "org.gnome.GPaste")]
        public class DBusServer : GLib.Object {
            [DBus (name = "GetHistory", inSignature = "", outSignature = "as")]
            public string[] get_history() {
                unowned GLib.SList<Item> history = History.instance.history;
                var as = new string[history.length()];
                int i = 0;
                foreach (Item item in history)
                    as[i++] = item.get_display_str ();
                return as;
            }

            [DBus (name = "Add", inSignature = "s", outSignature = "")]
            public void add(string selection) {
                if (selection != null && selection.validate ())
                    ClipboardsManager.instance.select (new TextItem (selection.strip ()));
            }

            [DBus (name = "GetElement", inSignature = "u", outSignature = "s")]
            public string get_element(uint32 index) {
                return History.instance.get_element(index);
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

            [DBus (name = "Track", inSignature = "b", outSignature = "")]
            public void track(bool tracking_state) {
                this.active = tracking_state;
                this.tracking(tracking_state);
            }

            [DBus (name = "Reexecute", inSignature = "", outSignature = "")]
            public void reexec() {
                Main.reexec();
            }

            [DBus (name = "Tracking", inSignature = "b")]
            public signal void tracking(bool tracking_state);

            [DBus (name = "Changed", inSignature = "")]
            public signal void changed();

            [DBus (name = "ToggleHistory", inSignature = "")]
            public signal void toggle_history();

            [DBus (name = "Active", signature = "b", access = "readonly")]
            public bool active {
                get {
                    return Settings.instance.track_changes;
                }
                private set {
                    Settings.instance.set_tracking_state(value);
                }
            }

            private DBusServer() {}

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
            private static GLib.MainLoop loop;

            private static void handle(int signal) {
                stdout.printf(_("Signal %d recieved, exiting.\n"), signal);
                Main.stop();
            }

            private static void on_bus_acquired(DBusConnection conn) {
                try {
                    conn.register_object("/org/gnome/GPaste", DBusServer.instance);
                } catch (IOError e) {
                    stderr.printf(_("Could not register DBus service.\n"));
                }
            }

            private static void start_dbus() {
                Bus.own_name(BusType.SESSION, "org.gnome.GPaste", BusNameOwnerFlags.NONE,
                    Main.on_bus_acquired, () => {}, () => {
                        stderr.printf(_("Could not aquire DBus name.\n"));
                        Posix.exit(1);
                    }
                );
            }

            private static void stop() {
                Keybinder.instance.unbind();
                Main.loop.quit();
            }

            public static void reexec() {
                Main.stop();
                Posix.execl(Config.PKGLIBEXECDIR + "/gpasted", "gpasted");
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
                cm.add_clipboard(clipboard);
                cm.add_clipboard(primary);
                cm.activate();
                var handler = Posix.sigaction_t();
                handler.sa_handler = Main.handle;
                Posix.sigaction(Posix.SIGTERM, handler, null);
                Posix.sigaction(Posix.SIGINT, handler, null);
                Keybinder.init(Settings.instance.keyboard_shortcut);
                Main.start_dbus();
                Main.loop = new GLib.MainLoop(null, false);
                Main.loop.run();
                return 0;
            }
        }

    }

}

