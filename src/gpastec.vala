namespace GPaste {

    [DBus (name = "org.gnome.GPaste", signature = "as")]
    interface GPastec : Object {
        public abstract string[] getHistory() throws IOError;
        public abstract void add(string selection, bool update_clipboards = false) throws IOError;
        public abstract void select (uint index) throws IOError;
    }

    public static int main(string[] args) {
        try {
            GPastec gpastec = Bus.get_proxy_sync(BusType.SESSION, "org.gnome.GPaste", "/org/gnome/gpaste");
            if (! Posix.isatty(stdin.fileno())) {
                var sb = new StringBuilder();
                sb.append(stdin.read_line());
                string s;
                while ((s = stdin.read_line()) != null) {
                    sb.append_c('\n');
                    sb.append(s);
                }
                gpastec.add(sb.str, true);
            } else if (args.length == 1) {
                string[] history = gpastec.getHistory();
                for (int i = 0 ; i < history.length ; ++i)
                    stdout.printf("\n%d:\n%s\n\n", i, history[i]);
            } else if (args.length == 3 && args[1] == "set") {
                gpastec.select(args[2].to_int());
            } else {
                gpastec.add(args[1]);
            }
        } catch (IOError e) {
            stderr.printf("%s\n", e.message);
        }
        return 0;
    }

}
