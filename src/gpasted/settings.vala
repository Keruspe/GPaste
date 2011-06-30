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

            private bool real_primary_to_history {
                get {
                    return this.settings.get_boolean("primary-to-history");
                }
            }

            private uint32 real_max_history_size {
                get {
                    return this.settings.get_value("max-history-size").get_uint32();
                }
            }

            private bool real_synchronize_clipboards {
                get {
                    return this.settings.get_boolean("synchronize-clipboards");
                }
            }

            public bool primary_to_history {
                get; private set;
            }

            public uint32 max_history_size {
                get; private set;
            }

            public bool synchronize_clipboards {
                get; private set;
            }

            public signal void changed(string key);

            private Settings() {
                this.settings = new GLib.Settings("org.gnome.GPaste");
                this.primary_to_history = real_primary_to_history;
                this.max_history_size = real_max_history_size;
                this.synchronize_clipboards = real_synchronize_clipboards;
                this.settings.changed.connect((key)=>{
                    switch(key) {
                    case "primary-to-history":
                        this.primary_to_history = real_primary_to_history;
                        break;
                    case "max-history-size":
                        this.max_history_size = real_max_history_size;
                        break;
                    case "synchronize-clipboards":
                        this.synchronize_clipboards = real_synchronize_clipboards;
                        break;
                    }
                });
            }
        }

    }

}
