namespace GPaste {

    [DBus (name = "org.gnome.GPaste")]
    public interface GPasteBusClient : Object {
        [DBus (signature = "as")]
        public abstract Variant getHistory() throws IOError;
        public abstract void delete(uint index) throws IOError;
        public abstract void select(uint index) throws IOError;
        public abstract signal void changed();
    }

    public class AppletWindow : Gtk.Window {
        private Gtk.StatusIcon tray_icon;
        private Gtk.Menu history;
        private Gtk.Menu options;

        public AppletWindow(Gtk.Application app) {
            Object(type: Gtk.WindowType.TOPLEVEL);
            title = "GPaste";
            application = app;
            set_resizable(false);
            tray_icon = new Gtk.StatusIcon.from_stock(Gtk.Stock.PASTE);
            tray_icon.set_tooltip_text("GPaste");
            tray_icon.set_visible(true);
            fill_history();
            (application as Applet).gpaste.changed.connect(fill_history);
            fill_options();
            tray_icon.button_press_event.connect(()=>{
                switch(Gtk.get_current_event().button.button) {
                case 1:
                    history.popup(null, null, null, 1, 0);
                    break;
                case 3:
                    options.popup(null, null, null, 3, 0);
                    break;
                }
                return false;
            });
        }

        private void fill_history() {
            history = new Gtk.Menu();
            try {
                var hist = (string[]) (application as Applet).gpaste.getHistory();
                for (uint i = 0 ; i < hist.length ; ++i) {
                    uint current = i; // local, or weird closure behaviour
                    var item = new Gtk.ImageMenuItem.with_mnemonic(hist[i]);
                    item.activate.connect(()=>{
                        try {
                            switch(Gtk.get_current_event().button.button) {
                            case 1:
                                (application as Applet).gpaste.select(current);
                                break;
                            case 3:
                                (application as Applet).gpaste.delete(current);
                                break;
                            }
                        } catch (IOError e) {
                            stderr.printf("Couldn't update history.\n");
                        }
                    });
                    history.add(item);
                }
            } catch (IOError e) {}
            history.show_all();
        }

        private void fill_options() {
            options = new Gtk.Menu();
            var preferences = new Gtk.ImageMenuItem.with_mnemonic("Preferences");
            preferences.activate.connect(()=>{
                try {
                    Process.spawn_command_line_async(Config.BINDIR + "/gpaste-preferences");
                } catch(SpawnError e) {
                    stderr.printf("Couldn't spawn gpaste-preferences.");
                }
            });
            options.add(preferences);
            var quit = new Gtk.ImageMenuItem.with_mnemonic("Quit");
            quit.activate.connect(()=>(application as GLib.Application).quit_mainloop());
            options.add(quit);
            options.show_all();
        }
    }

    public class Applet : Gtk.Application {
        public GPasteBusClient gpaste { get; private set; }

        public Applet() {
            Object(application_id: "org.gnome.GPaste.Applet");
            activate.connect(init);
        }

        private void init() {
            try {
                gpaste = Bus.get_proxy_sync(BusType.SESSION, "org.gnome.GPaste", "/org/gnome/GPaste");
            } catch (IOError e) {
                stderr.printf("Couldn't connect to GPaste.\n");
                Posix.exit(1);
            }
            new AppletWindow(this).hide();
        }

        public static int main(string[] args) {
            //Intl.bindtextdomain(Config.GETTEXT_PACKAGE, Config.LOCALEDIR);
            //Intl.textdomain(Config.GETTEXT_PACKAGE);
            Gtk.init(ref args);
            var app = new Applet();
            try {
                app.register();
            } catch (Error e) {
                stderr.printf("Fail\n");
                return 1;
            }
            return app.run();
        }
    }

}

