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

    namespace Applet {

        [DBus (name = "org.gnome.GPaste")]
        public interface DBusClient : GLib.Object {
            [DBus (signature = "as")]
            public abstract GLib.Variant getHistory() throws IOError;
            public abstract void delete(uint32 index) throws IOError;
            public abstract void select(uint32 index) throws IOError;
            public abstract void empty() throws IOError;
            public abstract void quit() throws IOError;
            public abstract signal void changed();
        }

        public class Window : Gtk.Window {
            private Gtk.StatusIcon tray_icon;
            private Gtk.Menu history;
            private Gtk.Menu options;
            private bool needs_repaint;

            public Window(Gtk.Application app) {
                GLib.Object(type: Gtk.WindowType.TOPLEVEL);
                this.application = app;
                this.tray_icon = new Gtk.StatusIcon.from_stock(Gtk.Stock.PASTE);
                this.tray_icon.set_tooltip_text("GPaste");
                this.tray_icon.set_visible(true);
                this.fill_history();
                (this.application as Main).gpaste.changed.connect(()=>{
                    this.needs_repaint = true;
                });
                this.fill_options();
                this.tray_icon.button_press_event.connect(()=>{
                    Gdk.Event e = Gtk.get_current_event();
                    switch(e.button.button) {
                    case 1:
                        if (this.needs_repaint)
                            this.fill_history();
                        this.history.popup(null, null, this.tray_icon.position_menu, 1, e.get_time());
                        break;
                    case 3:
                        this.options.popup(null, null, this.tray_icon.position_menu, 3, e.get_time());
                        break;
                    }
                    return false;
                });
            }

            public void fill_history() {
                this.history = new Gtk.Menu();
                bool history_is_empty;
                var app = this.application as Main;
                try {
                    var hist = app.gpaste.getHistory() as string[];
                    history_is_empty = (hist.length == 0);
                    uint32 element_size = app.element_size;
                    for (uint32 i = 0 ; i < hist.length ; ++i) {
                        uint32 current = i; // local, or weird closure behaviour
                        string elem = hist[i];
                        var item = new Gtk.ImageMenuItem.with_label(elem);
                        if (element_size != 0) {
                            var label = item.get_child() as Gtk.Label;
                            label.set_label(label.get_text().delimit("\n", ' '));
                            label.max_width_chars = (int)element_size;
                            label.ellipsize = Pango.EllipsizeMode.END;
                        }
                        item.activate.connect(()=>{
                            try {
                                switch(Gtk.get_current_event().button.button) {
                                case 1:
                                    app.gpaste.select(current);
                                    break;
                                case 3:
                                    app.gpaste.delete(current);
                                    break;
                                }
                            } catch (IOError e) {
                                stderr.printf(_("Couldn't update history.\n"));
                            }
                        });
                        this.history.add(item);
                    }
                } catch (IOError e) {}
                if (history_is_empty) {
                    var item = new Gtk.ImageMenuItem.with_label(_("(Empty)"));
                    var label = item.get_child() as Gtk.Label;
                    label.set_selectable(false);
                    this.history.add(item);
                }
                this.needs_repaint = false;
                this.history.show_all();
            }

            private void fill_options() {
                this.options = new Gtk.Menu();
                var preferences = new Gtk.ImageMenuItem.with_label(_("Preferences"));
                preferences.activate.connect(()=>{
                    try {
                        GLib.Process.spawn_command_line_async(Config.GPASTEEXECDIR + "/gpaste-preferences");
                    } catch(SpawnError e) {
                        stderr.printf(_("Couldn't spawn gpaste-preferences.\n"));
                    }
                });
                this.options.add(preferences);
                var empty = new Gtk.ImageMenuItem.with_label(_("Empty history"));
                empty.activate.connect(()=>{
                    try {
                        (this.application as Main).gpaste.empty();
                    } catch (IOError e) {
                        stderr.printf(_("Couldn't empty history.\n"));
                    }
                });
                this.options.add(empty);
                var quit = new Gtk.ImageMenuItem.with_label(_("Quit"));
                quit.activate.connect(()=>(this.application as GLib.Application).quit_mainloop());
                this.options.add(quit);
                this.options.show_all();
            }
        }

        public class Main : Gtk.Application {
            private GLib.Settings settings;
            public DBusClient gpaste { get; private set; }
            public uint32 element_size { get; private set; }
            private bool shutdown_on_exit;
            private Window window;

            public Main() {
                GLib.Object(application_id: "org.gnome.GPaste.Applet");
                this.settings = new GLib.Settings("org.gnome.GPaste");
                this.shutdown_on_exit = settings.get_boolean("shutdown-on-exit");
                this.element_size = settings.get_value("element-size").get_uint32();
                this.activate.connect(init);
                this.settings.changed.connect((key)=>{
                    switch(key) {
                    case "shutdown-on-exit":
                        this.shutdown_on_exit = settings.get_boolean("shutdown-on-exit");
                        break;
                    case "element-size":
                        this.element_size = settings.get_value("element-size").get_uint32();
                        this.window.fill_history(); /* Keep diplayed history up to date */
                        break;
                    }
                });
            }

            private void init() {
                try {
                    this.gpaste = Bus.get_proxy_sync(BusType.SESSION, "org.gnome.GPaste", "/org/gnome/GPaste");
                } catch (IOError e) {
                    stderr.printf(_("Couldn't connect to GPaste daemon.\n"));
                    Posix.exit(1);
                }
                this.window = new Window(this);
                this.window.hide();
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
                if (app.get_is_remote())
                    return 0;
                int ret = app.run();
                if (app.shutdown_on_exit) {
                    try {
                        app.gpaste.quit();
                    } catch (IOError e) {
                        stderr.printf(_("Couldn't shutdown daemon.\n"));
                    }
                }
                return ret;
            }
        }

    }

}
