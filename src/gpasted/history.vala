namespace GPaste {

    public class History : Object {
        private List<string> history;
        private static History singleton;

        public unowned List<string> getHistory() {
            return history;
        }

        private History() {
            history = new List<string>();
        }

        public static History getInstance() {
            if (singleton == null)
                singleton = new History();
            return singleton;
        }

        public void add(string selection) {
            for (unowned List<string?> s = history ; s != null ; s = s.next) {
                if (s.data == selection) {
                    history.remove_link(s);
                    break;
                }
            }
            history.prepend(selection);
            save();
        }

        public void select(uint index) {
            return_if_fail(index < history.length());
            string selection = history.nth_data(index);
            add(selection);
            ClipboardsManager.getInstance().select(selection);
        }

        public void load() {
            var history_file = File.new_for_path("gpasted.history");
            if (!history_file.query_exists()) {
                stderr.printf("Could not read history file\n");
                return;
            }

            try {
                int64 length;
                var dis = new DataInputStream(history_file.read());
                while((length = dis.read_int64()) != 0) {
                    var line = new StringBuilder();
                    for(int64 i = 0 ; i < length ; ++i) line.append_unichar(dis.read_byte());
                    history.append(line.str);
                }
            } catch (Error e) {
                stderr.printf("Could not read history file\n");
            }
        }

        public void save() {
            var history_file = File.new_for_path("gpasted.history");

            try {
                if (history_file.query_exists())
                    history_file.delete();
                var history_file_stream = history_file.create(FileCreateFlags.NONE);
                if (!history_file.query_exists()) {
                    stderr.printf("Could not create history file\n");
                    return;
                }
                var dos = new DataOutputStream(history_file_stream);
                foreach(string line in history) {
                    dos.put_int64(line.length);
                    dos.put_string(line);
                }
                dos.put_int64(0);
            } catch (Error e) {
                stderr.printf("Could not create history file\n");
            }
        }
    }

}

