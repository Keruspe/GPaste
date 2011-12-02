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

        public class Settings : GLib.Object {
            private GLib.Settings settings;

            private bool real_primary_to_history {
                get {
                    return this.settings.get_boolean("primary-to-history");
                }
            }

            private uint32 real_max_history_size {
                get {
                    return this.settings.get_value("max-history-size").get_uint32();
                }
            }

            private bool real_synchronize_clipboards {
                get {
                    return this.settings.get_boolean("synchronize-clipboards");
                }
            }

            private bool real_track_changes {
                get {
                    return this.settings.get_boolean("track-changes");
                }
            }

            private bool real_save_history {
                get {
                    return this.settings.get_boolean("save-history");
                }
            }

            private string real_keyboard_shortcut {
                owned get {
                    return this.settings.get_string("keyboard-shortcut");
                }
            }

            public bool primary_to_history {
                get; private set;
            }

            public uint32 max_history_size {
                get; private set;
            }

            public bool synchronize_clipboards {
                get; private set;
            }

            public bool track_changes {
                get; private set;
            }

            public bool save_history {
                get; private set;
            }

            public string keyboard_shortcut {
                get; private set;
            }

            public void set_tracking_state(bool state) {
                this.track_changes = state;
                this.settings.set_boolean("track-changes", state);
            }

            public signal void rebind (string binding);
            public signal void track (bool tracking_state);

            public Settings () {
                this.settings = new GLib.Settings("org.gnome.GPaste");
                this.primary_to_history = this.real_primary_to_history;
                this.max_history_size = this.real_max_history_size;
                this.synchronize_clipboards = this.real_synchronize_clipboards;
                this.track_changes = this.real_track_changes;
                this.save_history = this.real_save_history;
                this.keyboard_shortcut = this.real_keyboard_shortcut;
                this.settings.changed.connect((key)=>{
                    switch(key) {
                    case "primary-to-history":
                        this.primary_to_history = this.real_primary_to_history;
                        break;
                    case "max-history-size":
                        this.max_history_size = this.real_max_history_size;
                        break;
                    case "synchronize-clipboards":
                        this.synchronize_clipboards = this.real_synchronize_clipboards;
                        break;
                    case "track-changes":
                        this.track_changes = this.real_track_changes;
                        this.track (this.track_changes);
                        break;
                    case "save-history":
                        this.save_history = this.real_save_history;
                        break;
                    case "keyboard-shortcut":
                        this.keyboard_shortcut = this.real_keyboard_shortcut;
                        this.rebind (this.keyboard_shortcut);
                        break;
                    }
                });
            }
        }

    }

}

