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
        private GPaste.AppletMenu history;
        private GPaste.Client client;
        private Gtk.MenuPositionFunc? position;

        public Window(Gtk.Application app, Gtk.StatusIcon icon, Gtk.MenuPositionFunc? position) {
            GLib.Object (type: Gtk.WindowType.TOPLEVEL);
            this.application = app;
            this.position = position;
            icon.button_press_event.connect (() => {
                this.show_history ();
                return false;
            });
            try {
                this.client = new GPaste.Client.sync ();
                this.client.track_sync (true); /* In case we exited the applet and we're launching it back */
                this.client.show_history.connect (() => {
                    this.show_history ();
                });
            } catch (Error e) {
                stderr.printf ("%s: %s\n", _("Couldn't connect to GPaste daemon"), e.message);
                Posix.exit(1);
            }
        }

        private void show_history () {
            this.history = new GPaste.AppletMenu (this.client, this.application);
            try {
                var hist = this.client.get_history_sync ();
                for (uint i = 0 ; i < hist.length ; ++i) {
                    this.history.append (new GPaste.AppletItem (this.client, i));
                }
            } catch (GLib.Error e) {}
            this.history.show_all ();
            this.history.popup (null, null, this.position, 1, Gtk.get_current_event ().get_time ());
        }
    }

    public class Main : Gtk.Application {
        public Main() {
            GLib.Object (application_id: "org.gnome.GPaste.Applet");
            this.activate.connect (init);
        }

        private void init () {
            var tray_icon = new Gtk.StatusIcon.from_icon_name ("edit-paste");
            tray_icon.set_tooltip_text ("GPaste");
            tray_icon.set_visible (true);
            new Window (this, tray_icon, tray_icon.position_menu).hide ();
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
