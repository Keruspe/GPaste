/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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
            public unowned GLib.SList<Item> history {
                get;
                private set;
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
                this.history = new GLib.SList<Item>();
                DBusServer.instance.changed.connect(()=>{
                    this.save();
                });
            }

            private void remove (GLib.SList<Item> link) {
                var item = link.data;
                if (item is ImageItem) {
                    try {
                        GLib.File.new_for_path ((item as ImageItem).str).delete ();
                    } catch (GLib.Error e) {
                        stderr.printf ("Couldn't delete image file: %s\n", e.message);
                    }
                }
                this.history.remove_link(link);
            }

            public void add(Item selection) {
                if (!selection.has_value())
                    return;
                unowned GLib.SList<Item> s = this.history;
                if (s != null) {
                    if (s.data.equals(selection))
                        return;
                    for (s = s.next; s != null ; s = s.next) {
                        if (s.data.equals(selection)) {
                            this.remove (s);
                            break;
                        }
                    }
                }
                this.history.prepend(selection);
                uint32 max_history_size = Settings.instance.max_history_size;
                if (this.history.length() > max_history_size) {
                    unowned GLib.SList<Item> tmp = this.history;
                    for (uint32 i = 0 ; i < max_history_size ; ++i)
                        tmp = tmp.next;
                    do {
                        unowned GLib.SList<Item> next = tmp.next;
                        this.remove(tmp);
                        tmp = next;
                    } while(tmp != null);
                }
                DBusServer.instance.changed();
            }

            public void delete(uint32 index) {
                if (index >= this.history.length())
                    return;
                unowned GLib.SList<Item> tmp = this.history;
                for (uint32 i = 0 ; i < index ; ++i)
                    tmp = tmp.next;
                this.remove (tmp);
                if (index == 0)
                    this.select(0);
                DBusServer.instance.changed();
            }

            public string get_element(uint32 index) {
                if (index >= this.history.length())
                    return "";
                return this.history.nth_data(index).str;
            }

            public void select(uint32 index) {
                if (index >= this.history.length())
                    return;
                Item selection = this.history.nth_data(index);
                ClipboardsManager.instance.select(selection);
            }

            public void empty() {
                var history_file = GLib.File.new_for_path(GLib.Environment.get_home_dir() + "/.gpaste_history");
                try {
                    if (history_file.query_exists())
                        history_file.delete();
                } catch (Error e) {
                    stderr.printf(_("Could not delete history file.\n"));
                }
                this.history = new GLib.SList<Item>();
                DBusServer.instance.changed();
            }

            // TODO: Remove me after 2.0, once everyone'll have its history converted
            private void convert_old_history() {
                var history_file = GLib.File.new_for_path(GLib.Environment.get_user_data_dir() + "/gpaste/history");
                try {
                    int64 length;
                    var dis = new GLib.DataInputStream(history_file.read());
                    while((length = dis.read_int64()) != 0) {
                        var tmp_str = new uint8[length];
                        dis.read(tmp_str);
                        var str = (string) tmp_str;
                        if (str.validate())
                            this.history.append(new TextItem(str));
                    }
                    this.save();
                    history_file.delete();
                } catch (Error e) {
                    // File do no longer exist, we don't care about that
                }
                this.history = new GLib.SList<Item>();
            }

            // TODO: Remove me after 2.0, once everyone'll have its history converted
            public void convert_history() {
                this.convert_old_history();
                var history_file = GLib.File.new_for_path(GLib.Environment.get_home_dir() + "/.gpaste_history");
                try {
                    int64 length;
                    var dis = new GLib.DataInputStream(history_file.read());
                    while((length = dis.read_int64()) != 0) {
                        var kind = (ItemKind) dis.read_byte();
                        switch (kind) {
                        case ItemKind.TEXT:
                            var tmp_str = new uint8[length];
                            dis.read(tmp_str);
                            var str = (string) tmp_str;
                            if (str.validate())
                                this.history.append(new TextItem(str));
                            break;
                        case ItemKind.IMAGE:
                            // Was never supported with this serialization 
                            break;
                        }
                    }
                    this.save();
                    history_file.delete();
                } catch (Error e) {
                    // File do no longer exist, we don't care about that
                }
                this.history = new GLib.SList<Item>();
            }

            public void load() {
                this.convert_history();
                var history_file = GLib.Path.build_filename(GLib.Environment.get_user_data_dir(), "gpaste", "history.xml");
                var reader = new Xml.TextReader.filename(history_file);
                while (reader.read() == 1) {
                    if (reader.node_type() != 1 || reader.name() != "item")
                        continue;
                    string kind = reader.get_attribute("kind");
                    string date = reader.get_attribute("date");
                    if (kind == null)
                        kind = "Text";
                    string value = reader.read_string();
                    if (value == null || value.strip() == "" || !value.validate())
                        continue;
                    switch (kind) {
                    case "Text":
                        this.history.append(new TextItem(value));
                        break;
                    case "Uris":
                        this.history.append(new UrisItem(value));
                        break;
                    case "Image":
                        this.history.append(new ImageItem.load(value, new GLib.DateTime.from_unix_local (int64.parse (date))));
                        break;
                    }
                }
            }

            public void save() {
                string history_dir_path = GLib.Path.build_filename(GLib.Environment.get_user_data_dir(), "gpaste");
                var save_history = Settings.instance.save_history;
                if (!GLib.File.new_for_path(history_dir_path).query_exists()) {
                    if (!save_history)
                        return;
                    Posix.mkdir(history_dir_path, 0700);
                }

                var history_file = GLib.Path.build_filename(history_dir_path, "history.xml");
                try {
                    if (!save_history) {
                        GLib.File.new_for_path(history_file).delete();
                        return;
                    }
                    var writer = new Xml.TextWriter.filename(history_file);
                    writer.set_indent(true);
                    writer.set_indent_string("  ");

                    writer.start_document("1.0", "UTF-8");
                    writer.start_element("history");

                    foreach (Item i in this.history) {
                        writer.start_element("item");
                        writer.write_attribute("kind", i.get_kind ());
                        if (i is ImageItem)
                            writer.write_attribute("date", (i as ImageItem).date.to_unix ().to_string ());
                        writer.start_cdata();
                        writer.write_string(i.str);
                        writer.end_cdata();
                        writer.end_element();
                    }

                    writer.end_element();
                    writer.end_document();
                    writer.flush();
                } catch (Error e) {
                    stderr.printf(_("Could not create history file.\n"));
                }
            }
        }

    }

}

