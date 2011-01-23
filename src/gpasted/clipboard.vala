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

        public void select(string selection) {
            foreach(Clipboard c in clipboards) {
                c.real().set_text(selection, selection.length);
            }
        }

        private bool checkClipboards() {
            string text;
            foreach(Clipboard c in clipboards) {
                text = c.real().wait_for_text();
                if (text == null) {
                    unowned List<string> history = History.getInstance().getHistory();
                    if (history.length() == 0)
                        continue;
                    else {
                        string selection = history.nth_data(0);
                        c.real().set_text(selection, selection.length);
                    }
                }
                if (c.text != text) {
                    Gdk.Atom tmp = Gdk.SELECTION_CLIPBOARD; // Or valac will fail
                    c.text = text;
                    if (c.type == tmp)
                        History.getInstance().add(text);
                }
            }
            return true;
        }
    }

}

