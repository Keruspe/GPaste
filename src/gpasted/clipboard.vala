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

        public class Clipboard : GLib.Object {
            // For nautilus
            private static Gdk.Atom copy_files = Gdk.Atom.intern_static_string ("x-special/gnome-copied-files");

            public Gdk.Atom selection;

            public Gtk.Clipboard real {
                get;
                private set;
            }

            private string? _text;
            public string? txt {
                get {
                    return this._text;
                }
                set {
                    this._text = value;
                    this._image_checksum = null;
                }
            }

            private string? _image_checksum;
            public string? image_checksum {
                get {
                    return this._image_checksum;
                }
                set {
                    this._image_checksum = value;
                    this._text = null;
                }
            }

            public string? set_text () {
                string text = this.real.wait_for_text ();
                if (text == null || this.txt == text)
                    return null;
                this.txt = text;
                return text;
            }

            public void restore_text (string text) {
                this.txt = text;
                this.real.set_text (text, -1);
                this.real.store ();
            }

            public void select_text (Item item) {
                if (this.txt != item.get_value ());
                    this.restore_text (item.get_value ());
            }

            /* The two following callbacks are for restore_uris, adapted from diodon */

            private static void clear_clipboard_data (Gtk.Clipboard clipboard, void *user_data_or_owner) {}

            private static void get_clipboard_data (Gtk.Clipboard clipboard, Gtk.SelectionData selection_data, uint info, void *user_data_or_owner) {
                UrisItem item = user_data_or_owner as UrisItem;

                Gdk.Atom[] targets = new Gdk.Atom[1];
                targets[0] = selection_data.get_target ();

                // set content according to requested target
                if (Gtk.targets_include_text (targets)) {
                    selection_data.set_text (item.get_value (), -1);
                    return;
                }

                if (Gtk.targets_include_uri (targets))
                    selection_data.set_uris (item.get_uris ());
                else {
                    // set special nautilus target which should copy the files, 8 number of bits in a unit are used
                    string copy_files_str = "copy";
                    for (var i = 0; i < item.get_uris ().length; ++i)
                        copy_files_str += "\n" + item.get_uris ()[i];

                    var length = copy_files_str.length;
                    var copy_files_data = new uchar[length];
                    for (var i = 0; i < length; ++i)
                        copy_files_data[i] = (uchar) copy_files_str[i];

                    selection_data.set (copy_files, 8, copy_files_data);
                }
            }

            public void restore_uris (UrisItem item) {
                this.txt = item.get_value ();

                // The following stuff for this function is stolen from diodon

                // create default uri target and text target
                Gtk.TargetEntry[] targets = null;
                Gtk.TargetList target_list = new Gtk.TargetList (targets);
                target_list.add_text_targets (0);
                target_list.add_uri_targets (0);
                target_list.add (copy_files, 0, 0); // add special nautilus target
                targets = Gtk.target_table_new_from_list (target_list);

                // set data callbacks with a empty clear func as there is nothing to be cleared
                this.real.set_with_owner(targets, (Gtk.ClipboardGetFunc) get_clipboard_data, (Gtk.ClipboardClearFunc) clear_clipboard_data, item);
                this.real.store ();
            }

            public void select_uris (UrisItem item) {
                if (this.txt != item.get_value ());
                    this.restore_uris (item);
            }

            private void real_set_image (Gdk.Pixbuf image) {
                this.real.set_image (image);
                this.real.store ();
            }

            private bool _set_image (Gdk.Pixbuf image) {
                string checksum = GLib.Checksum.compute_for_data (GLib.ChecksumType.SHA256, (uchar[]) image.get_pixels ());
                if (this.image_checksum == checksum)
                    return false;
                this.image_checksum = checksum;
                return true;
            }

            public Gdk.Pixbuf? set_image () {
                Gdk.Pixbuf image = this.real.wait_for_image ();
                if (image != null && this._set_image (image))
                    return image;
                return null;
            }

            public void restore_image (Gdk.Pixbuf image) {
                if (image != null && this._set_image (image))
                    this.real_set_image (image);
            }

            public void select_image (ImageItem item) {
                if (this.image_checksum != item.get_checksum ()) {
                    this.image_checksum = item.get_checksum ();
                    this.real_set_image (item.get_image ());
                }
            }

            public Clipboard(Gdk.Atom type) {
                this.selection = type;
                this.real = Gtk.Clipboard.get(type);
                this._text = null;
                this._image_checksum = null;
            }
        }

        public class ClipboardsManager : GLib.Object {
            private GLib.SList<Clipboard> clipboards;
            private DBusServer gpasted = DBusServer.instance;

            private static ClipboardsManager _instance;
            public static ClipboardsManager instance {
                get {
                    if (ClipboardsManager._instance == null)
                        ClipboardsManager._instance = new ClipboardsManager();
                    return ClipboardsManager._instance;
                }
            }

            private ClipboardsManager() {
                this.clipboards = new GLib.SList<Clipboard>();
            }

            public void add_clipboard(Clipboard clipboard) {
                this.clipboards.prepend(clipboard);
                if (clipboard.real.wait_is_uris_available () || clipboard.real.wait_is_text_available())
                    clipboard.set_text ();
                else if (clipboard.real.wait_is_image_available())
                    clipboard.set_image ();
                if (clipboard.txt == null && clipboard.image_checksum == null) {
                    unowned GLib.SList<Item> history = History.instance.history;
                    if (history.length() != 0) {
                        Item item = history.data;
                        if (item is ImageItem)
                            clipboard.restore_image ((item as ImageItem).get_image ());
                        else if (item is UrisItem)
                            clipboard.restore_uris (item as UrisItem);
                        else /* TextItem */
                            clipboard.restore_text (item.get_value ());
                    }
                }
            }

            public void activate() {
                GLib.Timeout.add_seconds(1, this.check_clipboards);
            }

            public void select(Item selection) {
                History.instance.add(selection);
                foreach(Clipboard c in this.clipboards) {
                    if (selection is ImageItem)
                        c.select_image (selection as ImageItem);
                    else if (selection is UrisItem)
                        c.select_uris (selection as UrisItem);
                    else
                        c.select_text (selection);
                }
            }

            private bool check_clipboards() {
                string? synchronized_text = null;

                foreach(Clipboard c in this.clipboards) {
                    var something_in_clipboard = false;
                    var uris_available = c.real.wait_is_uris_available ();
                    if (uris_available || c.real.wait_is_text_available()) {
                        var text = c.set_text ();
                        something_in_clipboard = (c.txt != null);
                        if (text != null) {
                            Gdk.Atom tmp = Gdk.SELECTION_CLIPBOARD; // Or valac will fail
                            if (this.gpasted.active && (c.selection == tmp || Settings.instance.primary_to_history)) {
                                if (uris_available)
                                    History.instance.add(new UrisItem(text));
                                else
                                    History.instance.add(new TextItem(text));
                            }
                            if (Settings.instance.synchronize_clipboards)
                                synchronized_text = text;
                        }
                    } else if (c.real.wait_is_image_available()) {
                        var image = c.set_image ();
                        something_in_clipboard = (c.image_checksum != null);
                        if (image != null) {
                            Gdk.Atom tmp = Gdk.SELECTION_CLIPBOARD; // Or valac will fail
                            if (this.gpasted.active && (c.selection == tmp || Settings.instance.primary_to_history))
                                History.instance.add(new ImageItem(image));
                        }
                    }
                    if (!something_in_clipboard) {
                        unowned GLib.SList<Item> history = History.instance.history;
                        if (history.length() == 0)
                            continue;
                        Item selection = history.data;
                        if (selection is ImageItem)
                            c.select_image (selection as ImageItem);
                        else if (selection is UrisItem)
                            c.select_uris (selection as UrisItem);
                        else
                            c.select_text (selection);
                    }
                }
                if (synchronized_text != null) {
                    foreach(Clipboard c in this.clipboards) {
                        if (c.txt != synchronized_text)
                            c.restore_text (synchronized_text);
                    }
                }
                return true;
            }
        }

    }

}

