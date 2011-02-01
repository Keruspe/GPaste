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

    public class Clipboard : Object {
        public Gtk.Clipboard real { get; private set; }
        public Gdk.Atom selection;
        public string text;

        public Clipboard(Gdk.Atom type) {
            this.selection = type;
            this.real = Gtk.Clipboard.get(type);
        }
    }

    public class ClipboardsManager : Object {
        private List<Clipboard> clipboards;

        public bool primary_to_history;
        public bool synchronize_clipboards;

        private static ClipboardsManager _instance;
        public static ClipboardsManager instance {
            get {
                if (_instance == null)
                    _instance = new ClipboardsManager();
                return _instance;
            }
        }

        private ClipboardsManager() {
            clipboards = new List<Clipboard>();
            primary_to_history = GPastedSettings.primary_to_history;
            synchronize_clipboards = GPastedSettings.synchronize_clipboards;
            GPastedSettings.instance.changed.connect((key)=>{
                switch (key) {
                case "primary-to-history":
                    primary_to_history = GPastedSettings.primary_to_history;
                    break;
                case "synchronize-clipboards":
                    synchronize_clipboards = GPastedSettings.synchronize_clipboards;
                    break;
                }
            });
        }

        public void addClipboard(Clipboard clipboard) {
            clipboards.prepend(clipboard);
            clipboard.text = clipboard.real.wait_for_text();
            if (clipboard.text == null) {
                string text = History.instance.history.data;
                clipboard.text = text;
                clipboard.real.set_text(text, text.length);
            }
        }

        public void activate() {
            var time = new TimeoutSource(1000);
            time.set_callback(checkClipboards);
            time.attach(null);
        }

        public void select(string selection) {
            foreach(Clipboard c in clipboards) {
                c.real.set_text(selection, selection.length);
            }
        }

        private bool checkClipboards() {
            string? synchronized_text = null;
            foreach(Clipboard c in clipboards) {
                string text = c.real.wait_for_text();
                if (text == null) {
                    unowned List<string> history = History.instance.history;
                    if (history.length() == 0)
                        continue;
                    else {
                        string selection = history.nth_data(0);
                        c.real.set_text(selection, selection.length);
                    }
                }
                if (c.text != text) {
                    Gdk.Atom tmp = Gdk.SELECTION_CLIPBOARD; // Or valac will fail
                    c.text = text;
                    if (c.selection == tmp || primary_to_history)
                        History.instance.add(text);
                    if (synchronize_clipboards)
                        synchronized_text = text;
                }
            }
            if (synchronized_text != null) {
                foreach(Clipboard c in clipboards) {
                    if (c.text != synchronized_text) {
                        c.text = synchronized_text;
                        c.real.set_text(synchronized_text, synchronized_text.length);
                    }
                }
            }
            return true;
        }
    }

}

