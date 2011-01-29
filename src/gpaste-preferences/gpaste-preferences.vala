namespace GPaste {

    public class PreferencesWindow : Gtk.Window {
        private Gtk.CheckButton primary_to_history;
        private Gtk.SpinButton max_history_size;

        public PreferencesWindow(Gtk.Application app) {
            Object(type: Gtk.WindowType.TOPLEVEL);
            title = "GPaste Preferences";
            application = app;
            set_default_size(300, 70);
            set_position(Gtk.WindowPosition.CENTER);
            set_resizable(false);
            fill();
        }

        private void fill() {
            primary_to_history = new Gtk.CheckButton.with_mnemonic("Primary selection affects history");
            primary_to_history.set_active((application as Preferences).primary_to_history);
            primary_to_history.toggled.connect(()=>{
                (application as Preferences).primary_to_history = primary_to_history.get_active();
            });
            max_history_size = new Gtk.SpinButton.with_range(5, 100, 5);
            max_history_size.get_adjustment().value = (application as Preferences).max_history_size;
            max_history_size.get_adjustment().value_changed.connect(()=>{
                (application as Preferences).max_history_size = max_history_size.get_value_as_int();
            });
            var history_size_label = new Gtk.Label("Max history size: ");
            var hbox = new Gtk.HBox(false, 10);
            hbox.add(history_size_label);
            hbox.add(max_history_size);
            var vbox = new Gtk.VBox(false, 10);
            vbox.add(primary_to_history);
            vbox.add(hbox);
            add(vbox);
        }
    }

    public class Preferences : Gtk.Application {
        private Settings settings;

        public int max_history_size {
            get {
                return settings.get_int("max-history-size");
            }
            set {
                settings.set_int("max-history-size", value);
            }
        }

        public bool primary_to_history {
            get {
                return settings.get_boolean("primary-to-history");
            }
            set {
                settings.set_boolean("primary-to-history", value);
            }
        }

        public Preferences() {
            Object(application_id: "org.gnome.GPaste.Preferences");
            activate.connect(init);
        }

        private void init() {
            settings = new Settings("org.gnome.GPaste");
            new PreferencesWindow(this).show_all();
        }

        public static int main(string[] args) {
            Gtk.init(ref args);
            var app = new Preferences();
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

