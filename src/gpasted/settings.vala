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

        public class Settings : GLib.Object {
            private GLib.Settings settings;

            private static Settings _instance;
            public static Settings instance {
                get {
                    if (Settings._instance == null)
                        Settings._instance = new Settings();
                    return Settings._instance;
                }
            }

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

            public bool primary_to_history {
                get; private set;
            }

            public uint32 max_history_size {
                get; private set;
            }

            public bool synchronize_clipboards {
                get; private set;
            }

            public signal void changed(string key);

            private Settings() {
                this.settings = new GLib.Settings("org.gnome.GPaste");
                this.primary_to_history = real_primary_to_history;
                this.max_history_size = real_max_history_size;
                this.synchronize_clipboards = real_synchronize_clipboards;
                this.settings.changed.connect((key)=>{
                    switch(key) {
                    case "primary-to-history":
                        this.primary_to_history = real_primary_to_history;
                        break;
                    case "max-history-size":
                        this.max_history_size = real_max_history_size;
                        break;
                    case "synchronize-clipboards":
                        this.synchronize_clipboards = real_synchronize_clipboards;
                        break;
                    }
                });
            }
        }

    }

}
