namespace GPaste {

    namespace Daemon {

        public class Settings : GLib.Object {
            private GLib.Settings settings;

            private static Settings _instance;
            public static Settings instance {
                get {
                    if (Settings._instance == null)
                        Settings._instance = new Settings();
                    return Settings._instance;
                }
            }

            public bool primary_to_history {
                get {
                    return this.settings.get_boolean("primary-to-history");
                }
            }

            public int max_history_size {
                get {
                    return this.settings.get_int("max-history-size");
                }
            }

            public bool synchronize_clipboards {
                get {
                    return this.settings.get_boolean("synchronize-clipboards");
                }
            }

            public signal void changed(string key);

            private Settings() {
                this.settings = new GLib.Settings("org.gnome.GPaste");
                this.settings.changed.connect((key)=>this.changed(key));
            }
        }

    }

}
