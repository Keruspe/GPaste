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

    [DBus (name = "org.gnome.GPaste")]
    public class GPasteServer : Object {
        [DBus (signature = "as")]
        public Variant getHistory() {
            unowned List<string> history = History.instance.history;
            var vb = new VariantBuilder(new VariantType.array(VariantType.STRING));
            foreach (string s in history)
                vb.add_value(s);
            return vb.end();
        }

        public void add(string selection) {
            ClipboardsManager.instance.select(selection);
            History.instance.add(selection);
        }

        public void delete(uint index) {
            History.instance.delete(index);
        }

        public void select(uint index) {
            History.instance.select(index);
        }

        public void quit() {
            GPasted.loop.quit();
        }

        public signal void changed();

        private GPasteServer() {}
        private static GPasteServer _instance;
        public static GPasteServer instance {
            get {
                if (_instance == null)
                    _instance = new GPasteServer();
                return _instance;
            }
        }
    }

    public class GPasted : Object {
        public static MainLoop loop { get; private set; }

        private static void handle(int signal) {
            stdout.printf(_("Signal %d recieved, exiting.\n"), signal);
            loop.quit();
        }

        private static void on_bus_aquired(DBusConnection conn) {
            try {
                conn.register_object("/org/gnome/GPaste", GPasteServer.instance);
            } catch (IOError e) {
                stderr.printf(_("Could not register DBus service.\n"));
            }
        }

        private static void start_dbus() {
            Bus.own_name(BusType.SESSION, "org.gnome.GPaste", BusNameOwnerFlags.NONE,
                on_bus_aquired, () => {}, () => stderr.printf(_("Could not aquire DBus name.\n")));
        }

        public static int main(string[] args) {
            Intl.bindtextdomain(Config.GETTEXT_PACKAGE, Config.LOCALEDIR);
            Intl.textdomain(Config.GETTEXT_PACKAGE);
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
            start_dbus();
            loop = new MainLoop(null, false);
            loop.run();
            return 0;
        }
    }

}

