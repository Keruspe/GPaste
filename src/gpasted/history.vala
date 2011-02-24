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

    namespace Daemon {

        public class History : GLib.Object {
            private GLib.SList<string> _history;
            public GLib.SList<string> history {
                get {
                    return get_unowned_history();
                }
            }

            private static History _instance;
            public static History instance {
                get {
                    if (History._instance == null)
                        History._instance = new History();
                    return History._instance;
                }
            }

            private unowned GLib.SList<string> get_unowned_history() {
                return this._history;
            }

            public uint32 max_history_size;

            public virtual signal void changed() {
                this.save();
                DBusServer.instance.changed();
            }

            private History() {
                this._history = new GLib.SList<string>();
                Settings settings = Settings.instance;
                this.max_history_size = settings.max_history_size;
                settings.changed.connect((key)=>{
                    switch(key) {
                    case "max-history-size":
                        this.max_history_size = settings.max_history_size;
                        break;
                    }
                });
            }

            public void add(string selection) {
                if (selection == "")
                    return;
                for (unowned GLib.SList<string> s = history ; s != null ; s = s.next) {
                    if (s.data == selection) {
                        this._history.remove_link(s);
                        break;
                    }
                }
                this._history.prepend(selection);
                if (this._history.length() > this.max_history_size) {
                    unowned GLib.SList<string> tmp = this.history;
                    for (uint32 i = 0 ; i < this.max_history_size ; ++i)
                        tmp = tmp.next;
                    do {
                        unowned GLib.SList<string> next = tmp.next;
                        this._history.remove_link(tmp);
                        tmp = next;
                    } while(tmp != null);
                }
                this.changed();
            }

            public void delete(uint32 index) {
                if (index >= this._history.length())
                    return;
                unowned GLib.SList<string> tmp = this.history;
                for (uint32 i = 0 ; i < index ; ++i)
                    tmp = tmp.next;
                this._history.remove_link(tmp);
                if (index == 0)
                    this.select(0);
                else
                    this.changed();
            }

            public void select(uint32 index) {
                if (index >= this._history.length())
                    return;
                string selection = this._history.nth_data(index);
                this.add(selection);
                ClipboardsManager.instance.select(selection);
            }

            public void empty() {
                var history_file = GLib.File.new_for_path(Environment.get_user_data_dir() + "/gpaste/history");
                try {
                    if (history_file.query_exists())
                        history_file.delete();
                } catch (Error e) {
                    stderr.printf(_("Could not delete history file.\n"));
                }
                this._history = new GLib.SList<string>();
                this.changed();
            }

            public void load() {
                var history_file = GLib.File.new_for_path(Environment.get_user_data_dir() + "/gpaste/history");
                try {
                    int64 length;
                    var dis = new GLib.DataInputStream(history_file.read());
                    while((length = dis.read_int64()) != 0) {
                        var line = new GLib.StringBuilder();
                        uint8[] str = new uint8[length];
                        dis.read(str);
                        for(int64 i = 0 ; i < length ; ++i)
                            line.append_c((char) str[i]);
                        this._history.append(line.str);
                    }
                } catch (Error e) {
                    stderr.printf(_("Could not read history file.\n"));
                }
            }

            public void save() {
                string history_dir_path = Environment.get_user_data_dir() + "/gpaste";
                var history_dir = GLib.File.new_for_path(history_dir_path);
                if (!history_dir.query_exists())
                    Posix.mkdir(history_dir_path, 0700);

                var history_file = GLib.File.new_for_path(history_dir_path + "/history");
                try {
                    var history_file_stream = history_file.replace(null, false, GLib.FileCreateFlags.REPLACE_DESTINATION);
                    var dos = new GLib.DataOutputStream(history_file_stream);
                    foreach(string line in this._history) {
                        dos.put_int64(line.length);
                        dos.put_string(line);
                    }
                    dos.put_int64(0);
                } catch (Error e) {
                    stderr.printf(_("Could not create history file.\n"));
                }
            }
        }

    }

}
