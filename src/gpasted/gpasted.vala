namespace GPaste {

    [DBus (name = "org.gnome.GPaste", signature = "as")]
    public class GPasteServer : Object {

        public string[] getHistory() {
            unowned List<string> history = History.getInstance().getHistory();
            var vb = new VariantBuilder(new VariantType.array(VariantType.STRING));
            foreach (string s in history)
                vb.add_value(s);
            return (string[]) vb.end();
        }

    }

    public class MainClass : Object {
        private static MainLoop loop;

        private static void handle(int signal) {
            stdout.printf("Signal %d recieved, ", signal);
            loop.quit();
        }

        private static void on_bus_aquired(DBusConnection conn) {
            try {
                conn.register_object("/org/gnome/gpaste", new GPasteServer());
            } catch (IOError e) {
                stderr.printf("Could not register DBus service.\n");
            }
        }

        private static void start_dbus() {
            Bus.own_name(BusType.SESSION, "org.gnome.GPaste", BusNameOwnerFlags.NONE,
                on_bus_aquired, () => {}, () => stderr.printf("Could not aquire DBus name.\n"));
        }
    
        public static int main(string[] args) {
            Gtk.init(ref args);
            History.getInstance().load();
            var clipboard = new Clipboard(Gdk.SELECTION_CLIPBOARD);
            var primary = new Clipboard(Gdk.SELECTION_PRIMARY);
            var cm = ClipboardsManager.getInstance();
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
            stdout.printf("exiting...\n");
            return 0;
        }
    }
}

