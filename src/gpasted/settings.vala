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

            public uint32 max_history_size {
                get {
                    return this.settings.get_value("max-history-size").get_uint32();
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
