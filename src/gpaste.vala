namespace GPaste {

    [DBus (name = "org.gnome.GPaste")]
    interface GPaste : Object {
        [DBus (signature = "as")]
        public abstract Variant getHistory() throws IOError;
        public abstract void add(string selection) throws IOError;
        public abstract void select (uint index) throws IOError;
    }

    public static int main(string[] args) {
        try {
            GPaste gpaste = Bus.get_proxy_sync(BusType.SESSION, "org.gnome.GPaste", "/org/gnome/GPaste");
            if (! Posix.isatty(stdin.fileno())) {
                var sb = new StringBuilder();
                sb.append(stdin.read_line());
                string s;
                while ((s = stdin.read_line()) != null) {
                    sb.append_c('\n');
                    sb.append(s);
                }
                gpaste.add(sb.str);
            } else if (args.length == 1) {
                string[] history = (string[]) gpaste.getHistory();
                for (int i = 0 ; i < history.length ; ++i)
                    stdout.printf("%d: %s\n", i, history[i]);
            } else if (args.length == 3 && args[1] == "set") {
                gpaste.select(args[2].to_int());
            } else {
                gpaste.add(args[1]);
            }
        } catch (IOError e) {
            stderr.printf("%s\n", e.message);
        }
        return 0;
    }

}
