/*
 *      This program is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *      Copyright 2011 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      This file is part of GPaste.
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

namespace GPaste {

    namespace Daemon {

        public class History : GLib.Object {
            private GLib.SList<Item?> _history;
            public unowned GLib.SList<Item?> history {
                get {
                    return this._history;
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

            private History() {
                this._history = new GLib.SList<Item?>();
                DBusServer.instance.changed.connect(()=>{
                    this.save();
                });
            }

            public void add(Item selection) {
                // TODO: Handle images
                if (selection.val == null || selection.val /*.trim()*/ == "")
                    return;
                for (unowned GLib.SList<Item?> s = history ; s != null ; s = s.next) {
                    if (s.data == selection) {
                        this._history.remove_link(s);
                        break;
                    }
                }
                this._history.prepend(selection);
                uint32 max_history_size = Settings.instance.max_history_size;
                if (this._history.length() > max_history_size) {
                    unowned GLib.SList<Item?> tmp = this.history;
                    for (uint32 i = 0 ; i < max_history_size ; ++i)
                        tmp = tmp.next;
                    do {
                        unowned GLib.SList<Item?> next = tmp.next;
                        this._history.remove_link(tmp);
                        tmp = next;
                    } while(tmp != null);
                }
                DBusServer.instance.changed();
            }

            public void delete(uint32 index) {
                if (index >= this._history.length())
                    return;
                unowned GLib.SList<Item?> tmp = this.history;
                for (uint32 i = 0 ; i < index ; ++i)
                    tmp = tmp.next;
                this._history.remove_link(tmp);
                if (index == 0)
                    this.select(0);
                else
                    DBusServer.instance.changed();
            }

            public void select(uint32 index) {
                if (index >= this._history.length())
                    return;
                Item selection = this._history.nth_data(index);
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
                this._history = new GLib.SList<Item?>();
                DBusServer.instance.changed();
            }

            // TODO: Remove me for 2.0 once everyone'll have its history converted
            private void convertHistory() {
                var history_file = GLib.File.new_for_path(Environment.get_user_data_dir() + "/gpaste/history");
                try {
                    int64 length;
                    var dis = new GLib.DataInputStream(history_file.read());
                    while((length = dis.read_int64()) != 0) {
                        uint8[] str = new uint8[length];
                        dis.read(str);
                        this._history.append(Item(ItemKind.STRING, (string) str));
                    }
                    this.save();
                    history_file.delete();
                    GLib.File.new_for_path(Environment.get_user_data_dir() + "/gpaste").delete();
                } catch (Error e) {
                    // File do no longer exist, we don't care about that
                }
            }

            public void load() {
                this.convertHistory();
                var history_file = GLib.File.new_for_path(Environment.get_user_data_dir() + "/.gpaste_history");
                try {
                    int64 length;
                    var dis = new GLib.DataInputStream(history_file.read());
                    while((length = dis.read_int64()) != 0) {
                        ItemKind kind = (ItemKind) dis.read_byte();
                        uint8[] str = new uint8[length];
                        dis.read(str);
                        this._history.append(Item(kind, (string) str));
                    }
                } catch (Error e) {
                    stderr.printf(_("Could not read history file.\n"));
                }
            }

            public void save() {
                var history_file = GLib.File.new_for_path(Environment.get_user_data_dir() + "/.gpaste_history");
                try {
                    if (!Settings.instance.save_history) {
                        history_file.delete();
                        return;
                    }
                    var history_file_stream = history_file.replace(null, false, GLib.FileCreateFlags.REPLACE_DESTINATION);
                    var dos = new GLib.DataOutputStream(history_file_stream);
                    foreach(Item i in this._history) {
                        dos.put_int64(i.val.length);
                        dos.put_byte(i.kind);
                        dos.put_string(i.val);
                    }
                    dos.put_int64(0);
                } catch (Error e) {
                    stderr.printf(_("Could not create history file.\n"));
                }
            }
        }

    }

}
