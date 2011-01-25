/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *	
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *	
 *	You should have received a copy of the GNU General Public License
 *	along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *	
 *	Copyright 2011 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *	
 *	This file is part of GPaste.
 *	
 *	GPaste is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *	
 *	GPaste is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *	
 *	You should have received a copy of the GNU General Public License
 *	along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

namespace GPaste {

    public class History : Object {
        private List<string> history;
        private static History singleton;

        public virtual signal void changed() {
            /* TODO: How to propagate over DBus ? */
            save();
        }

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
                if (s.data == selection || GPastedSettings.primaryToHistory()) {
                    history.remove_link(s);
                    break;
                }
            }
            history.prepend(selection);
            if (history.length() > GPastedSettings.maxHistorySize()) {
                unowned List<string?> tmp = history;
                for (int i = 0 ; i < GPastedSettings.maxHistorySize() ; ++i)
                    tmp = tmp.next;
                do {
                    unowned List<string?> next = tmp.next;
                    history.remove_link(tmp);
                    tmp = next;
                } while(tmp != null);
            }
            changed();
        }

        public void select(uint index) {
            if (index >= history.length()) return;
            string selection = history.nth_data(index);
            add(selection);
            ClipboardsManager.getInstance().select(selection);
        }

        public void load() {
            string history_dir_path = Environment.get_user_data_dir() + "/gpaste";
            var history_dir = File.new_for_path(history_dir_path);
            if (!history_dir.query_exists()) {
                stderr.printf(_("Could not read history file\n"));
                Posix.mkdir(history_dir_path, 0700);
                return;
            }
            var history_file = File.new_for_path(history_dir_path + "/history");
            if (!history_file.query_exists()) {
                stderr.printf(_("Could not read history file\n"));
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
                stderr.printf(_("Could not read history file\n"));
            }
        }

        public void save() {
            var history_file = File.new_for_path(Environment.get_user_data_dir() + "/gpaste/history");
            try {
                if (history_file.query_exists())
                    history_file.delete();
                var history_file_stream = history_file.create(FileCreateFlags.NONE);
                if (!history_file.query_exists()) {
                    stderr.printf(_("Could not create history file\n"));
                    return;
                }
                var dos = new DataOutputStream(history_file_stream);
                foreach(string line in history) {
                    dos.put_int64(line.length);
                    dos.put_string(line);
                }
                dos.put_int64(0);
            } catch (Error e) {
                stderr.printf(_("Could not create history file\n"));
            }
        }
    }

}

