namespace GPaste {
    public class Clipboard : Object {
        private Gtk.Clipboard clipboard;

        public Gdk.Atom type;
        public string text;

        public Gtk.Clipboard real() {
            return this.clipboard;
        }

        public Clipboard(Gdk.Atom type) {
            this.type = type;
            this.clipboard = Gtk.Clipboard.get(type);
        }
    }

    public class ClipboardsManager : Object {
        private List<Clipboard> clipboards;
        private static ClipboardsManager singleton;

        private ClipboardsManager() {
            clipboards = new List<Clipboard>();
        }

        public static ClipboardsManager getInstance() {
            if (singleton == null)
                singleton = new ClipboardsManager();
            return singleton;
        }

        public void addClipboard(Clipboard clipboard) {
            clipboards.prepend(clipboard);
        }

        public void activate() {
            var time = new TimeoutSource(500);
            time.set_callback(checkClipboards);
            time.attach(null);
        }

        private bool checkClipboards() {
            string text;
            foreach(Clipboard c in clipboards) {
                text = c.real().wait_for_text();
                if (text == null) continue;
                if (c.text != text) {
                    Gdk.Atom tmp = Gdk.SELECTION_PRIMARY; // Or valac will fail
                    stdout.printf("Text changed for %s: %s\n", (c.type == tmp) ? "primary" : "clipboard", text);
                    c.text = text;
                }
            }
            return true;
        }
    }


    public class MainClass : Object {
        private static MainLoop loop;

        private static void handle(int signal) {
            stdout.printf("Signal %d recieved, ", signal);
            loop.quit();
        }
    
        public static int main(string[] args) {
            Gtk.init(ref args);
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
            loop = new MainLoop(null, false);
            loop.run();
            stdout.printf("exiting...\n");
            return 0;
        }
    }
}

