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

        public class ClipboardsManager : GLib.Object {
            private GLib.SList<Clipboard> clipboards;
            private History history;
            private Settings settings;

            public ClipboardsManager(History history, Settings settings) {
                this.history = history;
                this.settings = settings;
                this.history.selected.connect ((selection) =>{
                    this.select (selection);
                });
                this.clipboards = new GLib.SList<Clipboard>();
            }

            public void add_clipboard(Clipboard clipboard) {
                this.clipboards.prepend(clipboard);
                if (clipboard.get_real ().wait_is_uris_available () || clipboard.get_real ().wait_is_text_available())
                    clipboard.set_text ();
                else if (clipboard.get_real ().wait_is_image_available())
                    clipboard.set_image ();
                if (clipboard.get_text () == null && clipboard.get_image_checksum () == null) {
                    unowned GLib.SList<Item> history = this.history.history;
                    if (history.length() != 0)
                        clipboard.select_item (history.data);
                }
            }

            public void activate() {
                GLib.Timeout.add_seconds(1, this.check_clipboards);
            }

            public void select(Item selection) {
                this.history.add(selection);
                foreach(Clipboard c in this.clipboards) {
                    c.select_item (selection);
                }
            }

            private bool check_clipboards() {
                string? synchronized_text = null;

                foreach(Clipboard c in this.clipboards) {
                    var something_in_clipboard = false;
                    var uris_available = c.get_real ().wait_is_uris_available ();
                    if (uris_available || c.get_real ().wait_is_text_available()) {
                        var text = c.set_text ();
                        something_in_clipboard = (c.get_text () != null);
                        if (text != null) {
                            Gdk.Atom tmp = Gdk.SELECTION_CLIPBOARD; // Or valac will fail
                            if (this.settings.get_track_changes () && (c.get_target () == tmp || this.settings.get_primary_to_history ())) {
                                if (uris_available)
                                    this.history.add(new UrisItem(text));
                                else
                                    this.history.add(new TextItem(text));
                            }
                            if (this.settings.get_synchronize_clipboards ())
                                synchronized_text = text;
                        }
                    } else if (c.get_real ().wait_is_image_available()) {
                        var image = c.set_image ();
                        something_in_clipboard = (c.get_image_checksum () != null);
                        if (image != null) {
                            Gdk.Atom tmp = Gdk.SELECTION_CLIPBOARD; // Or valac will fail
                            if (this.settings.get_track_changes () && (c.get_target () == tmp || this.settings.get_primary_to_history ()))
                                this.history.add(new ImageItem(image));
                        }
                    }
                    if (!something_in_clipboard) {
                        unowned GLib.SList<Item> history = this.history.history;
                        if (history.length() == 0)
                            continue;
                        c.select_item (history.data);
                    }
                }
                if (synchronized_text != null) {
                    foreach(Clipboard c in this.clipboards) {
                        if (c.get_text () != synchronized_text)
                            c.select_text (synchronized_text);
                    }
                }
                return true;
            }
        }

    }

}

