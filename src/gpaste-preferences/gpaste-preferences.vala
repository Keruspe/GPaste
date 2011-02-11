namespace GPaste {

    namespace Preferences {

        public class Window : Gtk.Window {
            private Gtk.CheckButton primary_to_history_button;
            private Gtk.CheckButton synchronize_clipboards_button;
            private Gtk.CheckButton shutdown_on_exit_button;
            private Gtk.SpinButton max_history_size_button;
            private Gtk.SpinButton element_size_button;

            public bool primary_to_history {
                get {
                    return primary_to_history_button.get_active();
                }
                set {
                    primary_to_history_button.set_active(value);
                }
            }

            public bool synchronize_clipboards {
                get {
                    return synchronize_clipboards_button.get_active();
                }
                set {
                    synchronize_clipboards_button.set_active(value);
                }
            }

            public bool shutdown_on_exit {
                get {
                    return shutdown_on_exit_button.get_active();
                }
                set {
                    shutdown_on_exit_button.set_active(value);
                }
            }

            public int max_history_size {
                get {
                    return max_history_size_button.get_value_as_int();
                }
                set {
                    max_history_size_button.get_adjustment().value = value;
                }
            }

            public int element_size {
                get {
                    return element_size_button.get_value_as_int();
                }
                set {
                    element_size_button.get_adjustment().value = value;
                }
            }

            public Window(Gtk.Application app) {
                Object(type: Gtk.WindowType.TOPLEVEL);
                title = _("GPaste Preferences");
                application = app;
                set_default_size(300, 100);
                set_position(Gtk.WindowPosition.CENTER);
                set_resizable(false);
                fill();
            }

            private void fill() {
                var labels_vbox = new Gtk.VBox(true, 10);
                var history_size_label = new Gtk.Label(_("Max history size: "));
                var element_size_label = new Gtk.Label(_("Max element size when displaying: "));
                labels_vbox.add(history_size_label);
                labels_vbox.add(element_size_label);

                primary_to_history_button = new Gtk.CheckButton.with_mnemonic(_("_Primary selection affects history"));
                primary_to_history = (application as Main).primary_to_history;
                primary_to_history_button.toggled.connect(()=>{
                    (application as Main).primary_to_history = primary_to_history;
                });
                synchronize_clipboards_button = new Gtk.CheckButton.with_mnemonic(_("_Synchronize clipboard with primary selection"));
                synchronize_clipboards = (application as Main).synchronize_clipboards;
                synchronize_clipboards_button.toggled.connect(()=>{
                    (application as Main).synchronize_clipboards = synchronize_clipboards;
                });
                shutdown_on_exit_button = new Gtk.CheckButton.with_mnemonic(_("Shutdown the daemon when _quitting the applet"));
                shutdown_on_exit = (application as Main).shutdown_on_exit;
                shutdown_on_exit_button.toggled.connect(()=>{
                    (application as Main).shutdown_on_exit = shutdown_on_exit;
                });
                max_history_size_button = new Gtk.SpinButton.with_range(5, 255, 5);
                max_history_size = (application as Main).max_history_size;
                max_history_size_button.get_adjustment().value_changed.connect(()=>{
                    (application as Main).max_history_size = max_history_size;
                });
                element_size_button = new Gtk.SpinButton.with_range(0, 255, 5);
                element_size = (application as Main).element_size;
                element_size_button.get_adjustment().value_changed.connect(()=>{
                    (application as Main).element_size = element_size;
                });

                var values_vbox = new Gtk.VBox(true, 10);
                values_vbox.add(max_history_size_button);
                values_vbox.add(element_size_button);

                var values_hbox = new Gtk.HBox(false, 10);
                values_hbox.add(labels_vbox);
                values_hbox.add(values_vbox);

                var vbox = new Gtk.VBox(false, 10);
                vbox.add(primary_to_history_button);
                vbox.add(synchronize_clipboards_button);
                vbox.add(shutdown_on_exit_button);
                vbox.add(values_hbox);

                add(vbox);
            }
        }

        public class Main : Gtk.Application {
            private Settings settings;
            private Window window;

            public int max_history_size {
                get {
                    return settings.get_int("max-history-size");
                }
                set {
                    settings.set_int("max-history-size", value);
                }
            }

            public int element_size {
                get {
                    return settings.get_int("element-size");
                }
                set {
                    settings.set_int("element-size", value);
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

            public bool synchronize_clipboards {
                get {
                    return settings.get_boolean("synchronize-clipboards");
                }
                set {
                    settings.set_boolean("synchronize-clipboards", value);
                }
            }

            public bool shutdown_on_exit {
                get {
                    return settings.get_boolean("shutdown-on-exit");
                }
                set {
                    settings.set_boolean("shutdown-on-exit", value);
                }
            }

            public Main() {
                Object(application_id: "org.gnome.GPaste.Preferences");
                settings = new Settings("org.gnome.GPaste");
                activate.connect(init);
                settings.changed.connect((key)=>{
                    switch(key) {
                    case "max-history-size":
                        window.max_history_size = max_history_size;
                        break;
                    case "primary-to-history":
                        window.primary_to_history = primary_to_history;
                        break;
                    case "shutdown-on-exit":
                        window.shutdown_on_exit = shutdown_on_exit;
                        break;
                    }
                });
            }

            private void init() {
                window = new Window(this);
                window.show_all();
            }

            public static int main(string[] args) {
                Intl.bindtextdomain(Config.GETTEXT_PACKAGE, Config.LOCALEDIR);
                Intl.textdomain(Config.GETTEXT_PACKAGE);
                Gtk.init(ref args);
                var app = new Main();
                try {
                    app.register();
                } catch (Error e) {
                    stderr.printf(_("Fail to register the gtk application.\n"));
                    return 1;
                }
                return app.run();
            }
        }

    }

}
