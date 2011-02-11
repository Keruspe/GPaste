namespace GPaste {

    namespace Daemon {

        public class Settings : GLib.Settings {
            private static GLib.Settings _instance;
            public static GLib.Settings instance {
                get {
                    if (_instance == null)
                        _instance = new GLib.Settings("org.gnome.GPaste");
                    return _instance;
                }
            }

            public static bool primary_to_history {
                get {
                    return instance.get_boolean("primary-to-history");
                }
            }

            public static int max_history_size {
                get {
                    return instance.get_int("max-history-size");
                }
            }

            public static bool synchronize_clipboards {
                get {
                    return instance.get_boolean("synchronize-clipboards");
                }
            }

            private Settings() {}
        }

    }

}
