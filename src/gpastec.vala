namespace GPaste {

    [DBus (name = "org.gnome.GPaste", signature = "as")]
    interface GPastec : Object {
        public abstract string[] getHistory() throws IOError;
        public abstract void add(string selection) throws IOError;
    }

    public static int main(string[] args) {
        try {
            GPastec gpastec = Bus.get_proxy_sync(BusType.SESSION, "org.gnome.GPaste", "/org/gnome/gpaste");
            if (args.length == 1) {
                string[] history = gpastec.getHistory();
                for (int i = 0 ; i < history.length ; ++i)
                    stdout.printf("\n%d:\n%s\n\n", i+1, history[i]);
            } else {
                gpastec.add(args[1]);
            }
        } catch (IOError e) {
            stderr.printf("%s\n", e.message);
        }
        return 0;
    }

}
