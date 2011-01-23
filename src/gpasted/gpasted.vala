namespace GPaste {

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

