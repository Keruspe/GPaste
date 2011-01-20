namespace GPaste {

    public class History : Object {
        private List<string> history;
        private static History singleton;

        private History() {
            history = new List<string>();
        }

        public static History getInstance() {
            if (singleton == null)
                singleton = new History();
            return singleton;
        }

        public void add(string line) {
            foreach (string s in history)
                if (s == line) return;
            history.prepend(line);
            save();
        }

        public void load() {
            var history_file = File.new_for_path("gpasted.history");
            if (!history_file.query_exists()) {
                stderr.printf("Could not read history file\n");
                return;
            }

            string line;
            try {
                var dis = new DataInputStream(history_file.read());
                while((line = dis.read_line(null)) != null) {
                    stdout.printf("loaded: %s\n", line);
                    history.append(line);
                }
            } catch (Error e) {
                stderr.printf("Could not read history file\n");
            }
        }

        public void save() {
            var history_file = File.new_for_path("gpasted.history");

            try {
                if (history_file.query_exists())
                    history_file.delete();
                var history_file_stream = history_file.create(FileCreateFlags.NONE);
                if (!history_file.query_exists()) {
                    stderr.printf("Could not create history file\n");
                    return;
                }
                var dos = new DataOutputStream(history_file_stream);
                foreach(string line in history) {
                    stdout.printf("saved: %s\n", line);
                    dos.put_string(line);
                    dos.put_string("\n");
                }
            } catch (Error e) {
                stderr.printf("Could not create history file\n");
            }
        }
    }

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
                    Gdk.Atom tmp = Gdk.SELECTION_CLIPBOARD; // Or valac will fail
                    //stdout.printf("Text changed for %s: %s\n", (c.type != tmp) ? "primary" : "clipboard", text);
                    c.text = text;
                    if (c.type == tmp)
                        History.getInstance().add(text);
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
            loop = new MainLoop(null, false);
            loop.run();
            stdout.printf("exiting...\n");
            return 0;
        }
    }
}

