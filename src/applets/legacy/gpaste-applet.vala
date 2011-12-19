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

    static const string gettext_package = Config.GETTEXT_PACKAGE;

    namespace Applet {

        [DBus (name = "org.gnome.GPaste")]
        public interface DBusClient : GLib.Object {
            [DBus (name = "GetHistory", inSignature = "", outSignature = "as")]
            public abstract string[] get_history() throws GLib.IOError;
            [DBus (name = "Select", inSignature = "u", outSignature = "")]
            public abstract void select(uint32 index) throws GLib.IOError;
            [DBus (name = "Delete", inSignature = "u", outSignature = "")]
            public abstract void delete(uint32 index) throws GLib.IOError;
            [DBus (name = "Empty", inSignature = "", outSignature = "")]
            public abstract void empty() throws GLib.IOError;
            [DBus (name = "Track", inSignature = "b", outSignature = "")]
            public abstract void track(bool tracking_state) throws GLib.IOError;
            [DBus (name = "Changed", inSignature = "")]
            public abstract signal void changed();
            [DBus (name = "ToggleHistory", inSignature = "")]
            public abstract signal void toggle_history();
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
                (this.application as Main).gpasted.changed.connect(()=>{
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
                    var hist = app.gpasted.get_history();
                    history_is_empty = (hist.length == 0);
                    uint32 element_size = app.element_size;
                    for (uint32 index = 0 ; index < hist.length ; ++index) {
                        uint32 current = index; // local, or weird closure behaviour
                        string elem = hist[index];
                        var item = new Gtk.ImageMenuItem.with_label(elem);
                        var label = item.get_child() as Gtk.Label;
                        if (element_size != 0) {
                            label.set_label(label.get_text().delimit("\n", ' '));
                            label.max_width_chars = (int)element_size;
                            label.ellipsize = Pango.EllipsizeMode.END;
                        }
                        if (index == 0)
                            label.set_markup("<b>" + GLib.Markup.escape_text(label.get_text()) + "</b>");
                        item.activate.connect(()=>{
                            try {
                                switch(Gtk.get_current_event().button.button) {
                                case 1:
                                    app.gpasted.select(current);
                                    break;
                                case 3:
                                    app.gpasted.delete(current);
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

            public void toggle_history() {
                Gdk.Event e = Gtk.get_current_event();
                if (this.needs_repaint)
                    this.fill_history();
                this.history.popup(null, null, this.tray_icon.position_menu, 1, e.get_time());
            }

            private void fill_options() {
                this.options = new Gtk.Menu();
                var settings = new Gtk.ImageMenuItem.with_label(_("Settings"));
                settings.activate.connect(()=>{
                    try {
                        GLib.Process.spawn_command_line_async(Config.PKGLIBEXECDIR + "/gpaste-settings");
                    } catch(SpawnError e) {
                        stderr.printf(_("Couldn't spawn gpaste-settings.\n"));
                    }
                });
                this.options.add(settings);
                var empty = new Gtk.ImageMenuItem.with_label(_("Empty history"));
                empty.activate.connect(()=>{
                    try {
                        (this.application as Main).gpasted.empty();
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
            private Window window;
            private GLib.Settings settings;

            private uint32 real_element_size {
                get {
                    return this.settings.get_value("element-size").get_uint32();
                }
            }

            public uint32 element_size {
                get;
                private set;
            }

            public DBusClient gpasted {
                get;
                private set;
            }

            public Main() {
                GLib.Object(application_id: "org.gnome.GPaste.Applet");
                this.settings = new GLib.Settings("org.gnome.GPaste");
                this.element_size = this.real_element_size;
                this.activate.connect(init);
                this.settings.changed.connect((key)=>{
                    switch(key) {
                    case "element-size":
                        this.element_size = this.real_element_size;
                        this.window.fill_history(); /* Keep displayed history up to date */
                        break;
                    }
                });
            }

            private void init() {
                try {
                    this.gpasted = Bus.get_proxy_sync(BusType.SESSION, "org.gnome.GPaste", "/org/gnome/GPaste");
                    this.gpasted.track(true); /* In case we exited the applet and we're launching it back */
                    this.gpasted.toggle_history.connect(()=>{
                        this.window.toggle_history();
                    });
                } catch (IOError e) {
                    stderr.printf(_("Couldn't connect to GPaste daemon.\n"));
                    Posix.exit(1);
                }
                this.window = new Window(this);
                this.window.hide();
            }

            public static int main(string[] args) {
                GLib.Intl.bindtextdomain(gettext_package, Config.LOCALEDIR);
                GLib.Intl.bind_textdomain_codeset(gettext_package, "UTF-8");
                GLib.Intl.textdomain(gettext_package);
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
                return app.run();
            }
        }

    }

}

