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

    namespace Settings {

        public class Window : Gtk.Window {
            private Gtk.CheckButton primary_to_history_button;
            private Gtk.CheckButton synchronize_clipboards_button;
            private Gtk.CheckButton track_changes_button;
            private Gtk.CheckButton save_history_button;
            private Gtk.SpinButton max_history_size_button;
            private Gtk.SpinButton element_size_button;
            private Gtk.Entry keyboard_shortcut_entry;

            public bool primary_to_history {
                get {
                    return this.primary_to_history_button.get_active();
                }
                set {
                    this.primary_to_history_button.set_active(value);
                }
            }

            public bool synchronize_clipboards {
                get {
                    return this.synchronize_clipboards_button.get_active();
                }
                set {
                    this.synchronize_clipboards_button.set_active(value);
                }
            }

            public bool track_changes {
                get {
                    return this.track_changes_button.get_active();
                }
                set {
                    this.track_changes_button.set_active(value);
                }
            }

            public bool save_history {
                get {
                    return this.save_history_button.get_active();
                }
                set {
                    this.save_history_button.set_active(value);
                }
            }

            public uint32 max_history_size {
                get {
                    return (uint32)this.max_history_size_button.get_value_as_int();
                }
                set {
                    this.max_history_size_button.get_adjustment().value = value;
                }
            }

            public uint32 element_size {
                get {
                    return (uint32)this.element_size_button.get_value_as_int();
                }
                set {
                    this.element_size_button.get_adjustment().value = value;
                }
            }

            public string keyboard_shortcut {
                get {
                    return this.keyboard_shortcut_entry.get_text();
                }
                set {
                    this.keyboard_shortcut_entry.set_text(value);
                }
            }

            public Window(Gtk.Application app) {
                GLib.Object(type: Gtk.WindowType.TOPLEVEL);
                this.title = _("GPaste Settings");
                this.application = app;
                this.set_position(Gtk.WindowPosition.CENTER);
                this.resizable = false;
                this.fill();
            }

            private void fill() {
                var labels_vbox = new Gtk.VBox(false, 10);
                var history_size_label = new Gtk.Label(_("Max history size: "));
                var element_size_label = new Gtk.Label(_("Max element size when displaying: "));
                var keyboard_shortcut_label = new Gtk.Label(_("Keyboard shortcut to display the history: "));
                labels_vbox.add(history_size_label);
                labels_vbox.add(element_size_label);
                labels_vbox.add(keyboard_shortcut_label);

                var app = this.application as Main;

                this.primary_to_history_button = new Gtk.CheckButton.with_mnemonic(_("_Primary selection affects history"));
                this.primary_to_history = app.primary_to_history;
                this.primary_to_history_button.toggled.connect(()=>{
                    app.primary_to_history = this.primary_to_history;
                });
                this.synchronize_clipboards_button = new Gtk.CheckButton.with_mnemonic(_("_Synchronize clipboard with primary selection"));
                this.synchronize_clipboards = app.synchronize_clipboards;
                this.synchronize_clipboards_button.toggled.connect(()=>{
                    app.synchronize_clipboards = this.synchronize_clipboards;
                });
                this.track_changes_button = new Gtk.CheckButton.with_mnemonic(_("_Track clipboard changes"));
                this.track_changes = app.track_changes;
                this.track_changes_button.toggled.connect(()=>{
                    app.track_changes = this.track_changes;
                });
                this.save_history_button = new Gtk.CheckButton.with_mnemonic(_("_Save history"));
                this.save_history = app.save_history;
                this.save_history_button.toggled.connect(()=>{
                    app.save_history = this.save_history;
                });
                this.max_history_size_button = new Gtk.SpinButton.with_range(5, 255, 5);
                this.max_history_size = app.max_history_size;
                this.max_history_size_button.get_adjustment().value_changed.connect(()=>{
                    app.max_history_size = this.max_history_size;
                });
                this.element_size_button = new Gtk.SpinButton.with_range(0, 255, 5);
                this.element_size = app.element_size;
                this.element_size_button.get_adjustment().value_changed.connect(()=>{
                    app.element_size = this.element_size;
                });
                this.keyboard_shortcut_entry = new Gtk.Entry();
                this.keyboard_shortcut = app.keyboard_shortcut;
                this.keyboard_shortcut_entry.editing_done.connect(()=>{
                    app.keyboard_shortcut = this.keyboard_shortcut;
                });

                var values_vbox = new Gtk.VBox(true, 10);
                values_vbox.add(this.max_history_size_button);
                values_vbox.add(this.element_size_button);
                values_vbox.add(this.keyboard_shortcut_entry);

                var values_hbox = new Gtk.HBox(false, 10);
                values_hbox.add(labels_vbox);
                values_hbox.add(values_vbox);

                var vbox = new Gtk.VBox(false, 10);
                vbox.margin = 12;
                vbox.add(this.primary_to_history_button);
                vbox.add(this.synchronize_clipboards_button);
                vbox.add(this.track_changes_button);
                vbox.add(this.save_history_button);
                vbox.add(values_hbox);

                this.add(vbox);
            }
        }

        public class Main : Gtk.Application {
            private GLib.Settings settings;
            private Window window;

            public uint32 max_history_size {
                get {
                    return this.settings.get_value("max-history-size").get_uint32();
                }
                set {
                    this.settings.set_value("max-history-size", value);
                }
            }

            public uint32 element_size {
                get {
                    return this.settings.get_value("element-size").get_uint32();
                }
                set {
                    this.settings.set_value("element-size", value);
                }
            }

            public bool primary_to_history {
                get {
                    return this.settings.get_boolean("primary-to-history");
                }
                set {
                    this.settings.set_boolean("primary-to-history", value);
                }
            }

            public bool synchronize_clipboards {
                get {
                    return this.settings.get_boolean("synchronize-clipboards");
                }
                set {
                    this.settings.set_boolean("synchronize-clipboards", value);
                }
            }

            public bool track_changes {
                get {
                    return this.settings.get_boolean("track-changes");
                }
                set {
                    this.settings.set_boolean("track-changes", value);
                }
            }

            public bool save_history {
                get {
                    return this.settings.get_boolean("save-history");
                }
                set {
                    this.settings.set_boolean("save-history", value);
                }
            }

            public string keyboard_shortcut {
                owned get {
                    return this.settings.get_string("keyboard-shortcut");
                }
                set {
                    this.settings.set_string("keyboard-shortcut", value);
                }
            }

            public Main() {
                GLib.Object(application_id: "org.gnome.GPaste.Settings");
                this.activate.connect(this.init);
                this.settings = new GLib.Settings("org.gnome.GPaste");
                this.settings.changed.connect((key)=>{
                    switch(key) {
                    case "max-history-size":
                        this.window.max_history_size = this.max_history_size;
                        break;
                    case "element-size":
                        this.window.element_size = this.element_size;
                        break;
                    case "primary-to-history":
                        this.window.primary_to_history = this.primary_to_history;
                        break;
                    case "synchronize_clipboards":
                        this.window.synchronize_clipboards = this.synchronize_clipboards;
                        break;
                    case "track-changes":
                        this.window.track_changes = this.track_changes;
                        break;
                    case "save-history":
                        this.window.save_history = this.save_history;
                        break;
                    case "keyboard-shortcut":
                        this.window.keyboard_shortcut = this.keyboard_shortcut;
                        break;
                    }
                });
            }

            private void init() {
                this.window = new Window(this);
                this.window.show_all();
            }

            public static int main(string[] args) {
                GLib.Intl.bindtextdomain(Config.GETTEXT_PACKAGE, Config.LOCALEDIR);
                GLib.Intl.bind_textdomain_codeset(Config.GETTEXT_PACKAGE, "UTF-8");
                GLib.Intl.textdomain(Config.GETTEXT_PACKAGE);
                Gtk.init(ref args);
                var app = new Main();
                try {
                    app.register();
                } catch (Error e) {
                    stderr.printf(_("Fail to register the gtk application.\n"));
                    return 1;
                }
                return app.run();
            }
        }

    }

}

