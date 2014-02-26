/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

    public class Window : Gtk.Window {
        private Gtk.StatusIcon tray_icon;
        private GPaste.AppletHeader header;
        private Gtk.Menu history;
        private bool needs_repaint;

        public Window(Main app) {
            GLib.Object (type: Gtk.WindowType.TOPLEVEL);
            this.application = app;
            this.tray_icon = new Gtk.StatusIcon.from_icon_name ("edit-paste");
            this.tray_icon.set_tooltip_text ("GPaste");
            this.tray_icon.set_visible (true);
            this.header = new GPaste.AppletHeader (app.client);
            this.fill_history ();
            app.client.changed.connect (() => {
                this.needs_repaint = true;
            });
            this.tray_icon.button_press_event.connect (() => {
                this.show_history ();
                return false;
            });
        }

        public void fill_history () {
            var app = (Main) this.application;
            this.history = new Gtk.Menu ();
            this.header.add_to_menu (this.history);
            bool history_is_empty;
            try {
                var hist = app.client.get_history_sync ();
                history_is_empty = (hist.length == 0);
                uint element_size = app.element_size;
                for (uint i = 0 ; i < hist.length ; ++i) {
                    uint current = i; // local, or weird closure behaviour
                    string elem = hist[i];
                    var item = new Gtk.MenuItem.with_label (elem);
                    var label = (Gtk.Label) item.get_child ();
                    if (element_size != 0) {
                        label.set_label (label.get_text ().replace ("\n", " "));
                        label.max_width_chars = (int) element_size;
                        label.ellipsize = Pango.EllipsizeMode.END;
                    }
                    if (i == 0)
                        label.set_markup ("<b>" + GLib.Markup.escape_text (label.get_text ()) + "</b>");
                    item.button_release_event.connect ((event) => {
                        try {
                            switch (event.button) {
                            case Gdk.BUTTON_PRIMARY:
                                app.client.select_sync (current);
                                return false;
                            case Gdk.BUTTON_SECONDARY:
                                app.client.delete_sync (current);
                                this.show_history ();
                                return true;
                            }
                        } catch (Error e) {
                            stderr.printf (_("Couldn't update history.\n"));
                        }
                        return false;
                    });
                    this.history.add (item);
                }
            } catch (GLib.Error e) {}
            if (history_is_empty) {
                var item = new Gtk.MenuItem.with_label (_("(Empty)"));
                var label = (Gtk.Label) item.get_child ();
                label.set_selectable (false);
                this.history.add (item);
            }
            this.history.add (new Gtk.SeparatorMenuItem ());
            this.history.add (new GPaste.AppletEmpty (app.client));
            this.history.add (new GPaste.AppletSettings ());
            this.history.add (new GPaste.AppletQuit (this.application));
            this.needs_repaint = false;
            this.history.show_all ();
        }

        public void repaint () {
            this.header.remove_from_menu (this.history);
            this.fill_history ();
        }

        public void show_history () {
            if (this.needs_repaint)
                this.repaint ();
            this.history.popup (null, null, this.tray_icon.position_menu, 1, Gtk.get_current_event ().get_time ());
        }
    }

    public class Main : Gtk.Application {
        private Window window;
        private GPaste.Settings settings;

        public uint element_size {
            get;
            private set;
        }

        public Client client {
            get;
            private set;
        }

        public Main() {
            GLib.Object (application_id: "org.gnome.GPaste.Applet");
            this.settings = new GPaste.Settings ();
            try {
                this.client = new GPaste.Client.sync ();
            } catch (Error e) {
                stderr.printf ("%s: %s\n", _("Couldn't connect to GPaste daemon"), e.message);
                Posix.exit(1);
            }
            this.element_size = this.settings.get_element_size ();
            this.activate.connect (init);
            this.settings.changed.connect ((key) => {
                switch (key) {
                case "element-size":
                    this.element_size = this.settings.get_element_size ();
                    this.window.fill_history (); /* Keep displayed history up to date */
                    break;
                }
            });
        }

        private void init () {
            try {
                this.client.track_sync (true); /* In case we exited the applet and we're launching it back */
                this.client.show_history.connect (() => {
                    this.window.show_history ();
                });
            } catch (Error e) {
                stderr.printf ("%s: %s\n", _("Couldn't connect to GPaste daemon"), e.message);
                Posix.exit(1);
            }
            this.window = new Window (this);
            this.window.hide ();
        }

        public static int main (string[] args) {
            GLib.Intl.bindtextdomain (Config.GETTEXT_PACKAGE, Config.LOCALEDIR);
            GLib.Intl.bind_textdomain_codeset (Config.GETTEXT_PACKAGE, "UTF-8");
            GLib.Intl.textdomain (Config.GETTEXT_PACKAGE);
            Gtk.init (ref args);
            Gtk.Settings.get_default().gtk_application_prefer_dark_theme = true;
            var app = new Main ();
            try {
                app.register ();
            } catch (Error e) {
                stderr.printf (_("Fail to register the gtk application.\n"));
                return 1;
            }
            if (app.get_is_remote ())
                return 0;
            return app.run ();
        }
    }

}
