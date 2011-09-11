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

        public class Clipboard : GLib.Object {
            public Gdk.Atom selection;
            public string text;

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
                        string text = history.data.val;
                        clipboard.text = text;
                        clipboard.real.set_text(text, -1);
                    }
                }
            }

            public void activate() {
                var time = new TimeoutSource(1000);
                time.set_callback(this.check_clipboards);
                time.attach(null);
            }

            public void select(Item selection) {
                History.instance.add(selection);
                foreach(Clipboard c in this.clipboards) {
                    // TODO: Handle images
                    c.real.set_text(selection.val, -1);
                }
            }

            private bool check_clipboards() {
                // TODO: Handle images
                string? synchronized_text = null;
                foreach(Clipboard c in this.clipboards) {
                    string text = c.real.wait_for_text();
                    if (text == null) {
                        unowned GLib.SList<Item?> history = History.instance.history;
                        if (history.length() == 0)
                            continue;
                        Item selection = history.data;
                        c.real.set_text(selection.val, -1);
                    }
                    if (c.text != text) {
                        c.text = text;
                        Gdk.Atom tmp = Gdk.SELECTION_CLIPBOARD; // Or valac will fail
                        if (this.gpasted.active && (c.selection == tmp || Settings.instance.primary_to_history))
                            History.instance.add(Item(ItemKind.STRING, text));
                        if (Settings.instance.synchronize_clipboards)
                            synchronized_text = text;
                    }
                }
                if (synchronized_text != null) {
                    foreach(Clipboard c in this.clipboards) {
                        if (c.text != synchronized_text) {
                            c.text = synchronized_text;
                            c.real.set_text(synchronized_text, -1);
                        }
                    }
                }
                return true;
            }
        }

    }

}

