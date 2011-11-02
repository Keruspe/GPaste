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
            public string text;
            public Gdk.Pixbuf image;

            public Gtk.Clipboard real {
                get;
                private set;
            }

            public Clipboard(Gdk.Atom type) {
                this.selection = type;
                this.real = Gtk.Clipboard.get(type);
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
                clipboard.text = clipboard.real.wait_for_text();
                if (clipboard.text == null) {
                    unowned GLib.SList<Item?> history = History.instance.history;
                    if (history.length() != 0) {
                        //TODO: Handle images
                        string text = history.data.str;
                        clipboard.text = text;
                        clipboard.real.set_text(text, -1);
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
                        c.real.set_image((selection as ImageItem).img);
                    else /* TextItem */
                        c.real.set_text(selection.str, -1);
                }
            }

            private bool check_clipboards() {
                // TODO: Handle images
                string? synchronized_text = null;
                Gdk.Pixbuf? synchronized_image = null;
                foreach(Clipboard c in this.clipboards) {
                    var something_in_clipboard = false;
                    if (c.real.wait_is_text_available()) {
                        var text = c.real.wait_for_text();
                        if (text != null) {
                            something_in_clipboard = true;
                            if (c.text != text) {
                                c.text = text;
                                Gdk.Atom tmp = Gdk.SELECTION_CLIPBOARD; // Or valac will fail
                                if (this.gpasted.active && (c.selection == tmp || Settings.instance.primary_to_history))
                                    History.instance.add(new TextItem(text));
                                if (Settings.instance.synchronize_clipboards)
                                    synchronized_text = text;
                            }
                        }
                    } else if (c.real.wait_is_image_available()) {
                        var image = c.real.wait_for_image();
                        if (image != null) {
                            something_in_clipboard = true;
                            if (c.image != image) {
                                c.image = image;
                                Gdk.Atom tmp = Gdk.SELECTION_CLIPBOARD; // Or valac will fail
                                if (this.gpasted.active && (c.selection == tmp || Settings.instance.primary_to_history))
                                    History.instance.add(new ImageItem(image));
                                if (Settings.instance.synchronize_clipboards)
                                    synchronized_image = image;
                            }
                        }
                    }
                    if (!something_in_clipboard) {
                        unowned GLib.SList<Item?> history = History.instance.history;
                        if (history.length() == 0)
                            continue;
                        Item selection = history.data;
                        if (selection is ImageItem)
                            c.real.set_image((selection as ImageItem).img);
                        else /* TextItem */
                            c.real.set_text(selection.str, -1);
                    }
                }
                if (synchronized_text != null) {
                    foreach(Clipboard c in this.clipboards) {
                        if (c.text != synchronized_text) {
                            c.text = synchronized_text;
                            c.real.set_text(synchronized_text, -1);
                        }
                    }
                } else if (synchronized_image != null) {
                    foreach(Clipboard c in this.clipboards) {
                        if (c.image != synchronized_image) {
                            c.image = synchronized_image;
                            c.real.set_image(synchronized_image);
                        }
                    }
                }
                return true;
            }
        }

    }

}

