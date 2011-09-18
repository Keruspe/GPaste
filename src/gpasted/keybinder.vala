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

        public class Keybinder : GLib.Object {
            public static Keybinder instance {
                get;
                private set;
            }

            public int keycode {
                get;
                private set;
            }

            public Gdk.ModifierType modifiers {
                get;
                private set;
            }

            public static void init(string accelerator) {
                if (Keybinder.instance != null)
                    return;
                Keybinder.instance = new Keybinder(accelerator);
                Gdk.Window rootwin = Gdk.get_default_root_window();
                if(rootwin != null)
                    rootwin.add_filter(Keybinder.instance.event_filter);
            }

            private Keybinder(string accelerator) {
                this.activate(accelerator);
            }

            public void unbind() {
                Gdk.Window rootwin = Gdk.get_default_root_window();
                X.ID xid = Gdk.X11Window.get_xid(rootwin);
                unowned X.Display display = Gdk.x11_get_default_xdisplay();
                display.ungrab_key(this.keycode, this.modifiers, xid);
            }

            public void rebind(string accelerator) {
                this.unbind();
                this.activate(accelerator);
            }

            private void activate(string accelerator) {
                uint keysym;
                Gdk.ModifierType mod;
                Gtk.accelerator_parse(accelerator, out keysym, out mod);

                unowned X.Display display = Gdk.x11_get_default_xdisplay();
                this.keycode = display.keysym_to_keycode(keysym);
                this.modifiers = mod;
                if(this.keycode != 0) {
                    Gdk.error_trap_push();
                    Gdk.Window rootwin = Gdk.get_default_root_window();
                    X.ID xid = Gdk.X11Window.get_xid(rootwin);
                    display.grab_key(this.keycode, this.modifiers, xid, false, X.GrabMode.Async, X.GrabMode.Async);
                    Gdk.flush();
                    Gdk.error_trap_pop_ignored();
                }
            }

            private Gdk.FilterReturn event_filter(Gdk.XEvent gdk_xevent, Gdk.Event gdk_event) {
                var xevent = *((X.Event*)(&gdk_xevent));
                if(xevent.type == X.EventType.KeyPress && xevent.xkey.keycode == this.keycode && xevent.xkey.state == this.modifiers)
                    DBusServer.instance.toggle_history();
                return Gdk.FilterReturn.CONTINUE;
            }
        }

    }

}

