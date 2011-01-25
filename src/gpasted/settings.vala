namespace GPaste {

    public class GPastedSettings : Settings {
        private static Settings singleton;

        public static Settings getInstance() {
            if (singleton == null)
                singleton = new Settings("org.gnome.GPaste");
            return singleton;
        }

        public static bool primaryToHistory() {
            return getInstance().get_boolean("primary-to-history");
        }

        public static int maxHistorySize() {
            return getInstance().get_int("max-history-size");
        }

        private GPastedSettings() {}
    }

}

