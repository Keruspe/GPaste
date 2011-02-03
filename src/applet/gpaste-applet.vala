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

    [DBus (name = "org.gnome.GPaste")]
    public interface GPasteBusClient : Object {
        [DBus (signature = "as")]
        public abstract Variant getHistory() throws IOError;
        public abstract void delete(uint index) throws IOError;
        public abstract void select(uint index) throws IOError;
        public abstract void empty() throws IOError;
        public abstract void quit() throws IOError;
        public abstract signal void changed();
    }

    public class AppletWindow : Gtk.Window {
        private Gtk.StatusIcon tray_icon;
        private Gtk.Menu history;
        private Gtk.Menu options;

        public AppletWindow(Gtk.Application app) {
            Object(type: Gtk.WindowType.TOPLEVEL);
            title = "GPaste";
            application = app;
            set_resizable(false);
            tray_icon = new Gtk.StatusIcon.from_stock(Gtk.Stock.PASTE);
            tray_icon.set_tooltip_text("GPaste");
            tray_icon.set_visible(true);
            fill_history();
            (application as Applet).gpaste.changed.connect(fill_history);
            fill_options();
            tray_icon.button_press_event.connect(()=>{
                Gdk.Event e = Gtk.get_current_event();
                switch(e.button.button) {
                case 1:
                    history.popup(null, null, tray_icon.position_menu, 1, e.get_time());
                    break;
                case 3:
                    options.popup(null, null, tray_icon.position_menu, 3, e.get_time());
                    break;
                }
                return false;
            });
        }

        private void fill_history() {
            history = new Gtk.Menu();
            bool history_is_empty;
            try {
                var hist = (string[]) (application as Applet).gpaste.getHistory();
                history_is_empty = (hist.length == 0);
                int element_size = (application as Applet).element_size;
                for (uint i = 0 ; i < hist.length ; ++i) {
                    uint current = i; // local, or weird closure behaviour
                    string elem = hist[i];
                    if (element_size != 0) {
                        elem = elem.delimit("\n", ' ');
                        if (elem.length > element_size)
                            elem = elem.substring(0, element_size-1) + "…";
                    }
                    var item = new Gtk.ImageMenuItem.with_label(elem);
                    item.activate.connect(()=>{
                        try {
                            switch(Gtk.get_current_event().button.button) {
                            case 1:
                                (application as Applet).gpaste.select(current);
                                break;
                            case 3:
                                (application as Applet).gpaste.delete(current);
                                break;
                            }
                        } catch (IOError e) {
                            stderr.printf(_("Couldn't update history.\n"));
                        }
                    });
                    history.add(item);
                }
            } catch (IOError e) {}
            if (history_is_empty) {
                var item = new Gtk.ImageMenuItem.with_label(_("(Empty)"));
                history.add(item);
            }
            history.show_all();
        }

        private void fill_options() {
            options = new Gtk.Menu();
            var preferences = new Gtk.ImageMenuItem.with_label(_("Preferences"));
            preferences.activate.connect(()=>{
                try {
                    Process.spawn_command_line_async(Config.BINDIR + "/gpaste-preferences");
                } catch(SpawnError e) {
                    stderr.printf(_("Couldn't spawn gpaste-preferences.\n"));
                }
            });
            options.add(preferences);
            var empty = new Gtk.ImageMenuItem.with_label(_("Empty history"));
            empty.activate.connect(()=>{
                try {
                    (application as Applet).gpaste.empty();
                } catch (IOError e) {
                    stderr.printf(_("Couldn't empty history.\n"));
                }
            });
            options.add(empty);
            var quit = new Gtk.ImageMenuItem.with_label(_("Quit"));
            quit.activate.connect(()=>(application as GLib.Application).quit_mainloop());
            options.add(quit);
            options.show_all();
        }
    }

    public class Applet : Gtk.Application {
        private Settings settings;
        public GPasteBusClient gpaste { get; private set; }
        public int element_size { get; private set; }
        private bool shutdown_on_exit;

        public Applet() {
            Object(application_id: "org.gnome.GPaste.Applet");
            settings = new Settings("org.gnome.GPaste");
            shutdown_on_exit = settings.get_boolean("shutdown-on-exit");
            element_size = settings.get_int("element-size");
            activate.connect(init);
            settings.changed.connect((key)=>{
                switch(key) {
                case "shutdown-on-exit":
                    shutdown_on_exit = settings.get_boolean("shutdown-on-exit");
                    break;
                case "element-size":
                    element_size = settings.get_int("element-size");
                    break;
                }
            });
        }

        private void init() {
            try {
                gpaste = Bus.get_proxy_sync(BusType.SESSION, "org.gnome.GPaste", "/org/gnome/GPaste");
            } catch (IOError e) {
                stderr.printf(_("Couldn't connect to GPaste.\n"));
                Posix.exit(1);
            }
            new AppletWindow(this).hide();
        }

        public static int main(string[] args) {
            Intl.bindtextdomain(Config.GETTEXT_PACKAGE, Config.LOCALEDIR);
            Intl.textdomain(Config.GETTEXT_PACKAGE);
            Gtk.init(ref args);
            var app = new Applet();
            try {
                app.register();
            } catch (Error e) {
                stderr.printf(_("Fail to register the gtk application.\n"));
                return 1;
            }
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

