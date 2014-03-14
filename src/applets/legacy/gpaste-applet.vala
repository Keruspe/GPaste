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
        private GPaste.AppletMenu menu;
        private GPaste.AppletHistory history;
        private GPaste.AppletStatusIcon status_icon;
        private GPaste.Client client;

        public Window(Gtk.Application app) {
            GLib.Object (type: Gtk.WindowType.TOPLEVEL);
            this.application = app;
            try {
                this.client = new GPaste.Client.sync ();
                this.client.track_sync (true); /* In case we exited the applet and we're launching it back */
            } catch (Error e) {
                stderr.printf ("%s: %s\n", _("Couldn't connect to GPaste daemon"), e.message);
                Posix.exit(1);
            }
            this.menu = new GPaste.AppletMenu (this.client, this.application);
            this.history = new GPaste.AppletHistory.sync (this.client, this.menu);
            this.status_icon = new GPaste.AppletStatusIcon (this.client, this.menu);
        }
    }

    public class Main : Gtk.Application {
        public Main() {
            GLib.Object (application_id: "org.gnome.GPaste.Applet");
            this.activate.connect (init);
        }

        private void init () {
            new Window (this).hide ();
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
