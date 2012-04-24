/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2012 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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
    public class Main : Gtk.Application {
        public Main () {
            GLib.Object (application_id: "org.gnome.GPaste.Settings");

            this.activate.connect (this.init);
        }

        private void init() {
            new Window (this).show_all ();
        }

        public static int main (string[] args) {
            const string gettext_package = Config.GETTEXT_PACKAGE;
            GLib.Intl.bindtextdomain (gettext_package, Config.LOCALEDIR);
            GLib.Intl.bind_textdomain_codeset (gettext_package, "UTF-8");
            GLib.Intl.textdomain (gettext_package);
            Gtk.init (ref args);
            Gtk.Settings.get_default ().gtk_application_prefer_dark_theme = true;

            var app = new Main ();
            try {
                app.register ();
            } catch (Error e) {
                stderr.printf(_("Fail to register the gtk application.\n"));
                return 1;
            }
            return app.run ();
        }
    }
}
