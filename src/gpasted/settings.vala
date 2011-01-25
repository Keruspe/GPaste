namespace GPaste {

    public class GPastedSettings : Settings {
        private static Settings _instance;
        public static Settings instance {
            get {
                if (_instance == null)
                    _instance = new Settings("org.gnome.GPaste");
                return _instance;
            }
        }

        public static bool primaryToHistory() {
            return instance.get_boolean("primary-to-history");
        }

        public static int maxHistorySize() {
            return instance.get_int("max-history-size");
        }

        private GPastedSettings() {}
    }

}

