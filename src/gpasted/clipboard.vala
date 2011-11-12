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
                if (text != null) {
                    this.txt = text;
                    this.real.set_text (text, -1);
                }
            }

            public void select_text (TextItem item) {
                if (this.txt != item.str);
                    this.restore_text (item.str);
            }

            public void restore_uris (string uris) {
                if (uris != null) {
                    this.txt = uris;
                    // TODO: this.real.set_uris (uris);
                    this.real.set_text (uris, -1);
                }
            }

            public void select_uris (UrisItem item) {
                if (this.txt != item.str);
                    this.restore_uris (item.str);
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
                    this.real.set_image (image);
            }

            public void select_image (ImageItem item) {
                if (this.image_checksum != item.checksum) {
                    this.image_checksum = item.checksum;
                    this.real.set_image (item.img);
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
                            clipboard.restore_image ((item as ImageItem).img);
                        else if (item is UrisItem)
                            clipboard.restore_uris (item.str);
                        else /* TextItem */
                            clipboard.restore_text (item.str);
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
                        c.select_text (selection as TextItem);
                }
            }

            private bool check_clipboards() {
                string? synchronized_text = null;

                foreach(Clipboard c in this.clipboards) {
                    var something_in_clipboard = false;
                    var uris_available = c.real.wait_is_uris_available ();
                    if (uris_available || c.real.wait_is_text_available()) {
                        var text = c.set_text ();
                        if (text != null) {
                            something_in_clipboard = true;
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
                        if (image != null) {
                            something_in_clipboard = true;
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
                            c.select_text (selection as TextItem);
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

