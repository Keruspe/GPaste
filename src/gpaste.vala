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

    namespace Client {

        [DBus (name = "org.gnome.GPaste")]
        interface DBusClient : GLib.Object {
            [DBus (name = "GetHistory", inSignature = "", outSignature = "as")]
            public abstract GLib.Variant get_history() throws GLib.IOError;
            [DBus (name = "Add", inSignature = "s", outSignature = "")]
            public abstract void add(string selection) throws GLib.IOError;
            [DBus (name = "GetElement", inSignature = "u", outSignature = "s")]
            public abstract string get_element(uint32 index) throws GLib.IOError;
            [DBus (name = "Select", inSignature = "u", outSignature = "")]
            public abstract void select(uint32 index) throws GLib.IOError;
            [DBus (name = "Delete", inSignature = "u", outSignature = "")]
            public abstract void delete(uint32 index) throws GLib.IOError;
            [DBus (name = "Empty", inSignature = "", outSignature = "")]
            public abstract void empty() throws GLib.IOError;
            [DBus (name = "Track", inSignature = "b", outSignature = "")]
            public abstract void track(bool tracking_state) throws GLib.IOError;
            [DBus (name = "Reexecute", inSignature = "", outSignature = "")]
            public abstract void reexec() throws GLib.IOError;
        }

        public class Main : GLib.Object {
            private void usage(string caller) {
                stdout.printf(_("Usage:\n"));
                stdout.printf(_("%s: print the history\n"), caller);
                stdout.printf(_("%s [add] <text>: set text to clipboard\n"), caller);
                stdout.printf(_("%s get <number>: get the <number>th item from the history\n"), caller);
                stdout.printf(_("%s set <number>: set the <number>th item from the history to the clipboard\n"), caller);
                stdout.printf(_("%s delete <number>: delete <number>th item of the history\n"), caller);
                stdout.printf(_("%s -f/--file <path>: put the content of the file at <path> into the clipboard\n"), caller);
                stdout.printf(_("whatever | %s: set the output of whatever to clipboard\n"), caller);
                stdout.printf(_("%s empty: empty the history\n"), caller);
                stdout.printf(_("%s start: start tracking clipboard changes\n"), caller);
                stdout.printf(_("%s stop: stop tracking clipboard changes\n"), caller);
                stdout.printf(_("%s quit: alias for stop\n"), caller);
                stdout.printf(_("%s daemon-reexec: reexecute the daemon (after upgrading...)\n"), caller);
#if ENABLE_APPLET
                stdout.printf(_("%s applet: launch the applet\n"), caller);
#endif
                stdout.printf(_("%s settings: launch the configuration tool\n"), caller);
                stdout.printf(_("%s version/-v/--version: display the version\n"), caller);
                stdout.printf(_("%s help: display this help\n"), caller);
            }

            public static int main(string[] args) {
                GLib.Intl.bindtextdomain(Config.GETTEXT_PACKAGE, Config.LOCALEDIR);
                GLib.Intl.bind_textdomain_codeset(Config.GETTEXT_PACKAGE, "UTF-8");
                GLib.Intl.textdomain(Config.GETTEXT_PACKAGE);
                GLib.Intl.setlocale(LocaleCategory.ALL, "");
                var app = new Main();
                try {
                    DBusClient gpaste = Bus.get_proxy_sync(BusType.SESSION, "org.gnome.GPaste", "/org/gnome/GPaste");
                    if (!Posix.isatty(stdin.fileno())) {
                        /* We are being piped ! */
                        var text = new StringBuilder();
                        string line = stdin.read_line();
                        if (line != null)
                            text.append(line);
                        while ((line = stdin.read_line()) != null)
                            text.append_c('\n').append(line);
                        gpaste.add(text.str);
                    } else {
                        switch (args.length) {
                        case 1:
                            var history = gpaste.get_history() as string[];
                            for (int i = 0 ; i < history.length ; ++i)
                                stdout.printf("%d: %s\n", i, history[i]);
                            break;
                        case 2:
                            switch (args[1]) {
                            case "help":
                            case "-h":
                            case "--help":
                                app.usage(args[0]);
                                break;
                            case "start":
                            case "daemon":
                                gpaste.track(true);
                                break;
                            case "stop":
                            case "quit":
                                gpaste.track(false);
                                break;
                            case "empty":
                                gpaste.empty();
                                break;
                            case "version":
                            case "-v":
                            case "--version":
                                stdout.printf(Config.PACKAGE_STRING+"\n");
                                break;
#if ENABLE_APPLET
                            case "applet":
                                try {
                                    GLib.Process.spawn_command_line_async(Config.PKGLIBEXECDIR + "/gpaste-applet");
                                } catch(SpawnError e) {
                                    stderr.printf(_("Couldn't spawn gpaste-applet.\n"));
                                }
                                break;
#endif
                            case "settings":
                            case "preferences":
                                Posix.execl(Config.PKGLIBEXECDIR + "/gpaste-settings", "GPaste-Settings");
                                break;
                            case "daemon-reexec":
                                try {
                                    gpaste.reexec();
                                } catch (GLib.Error e) {
                                    if (e.code == 4) /* NoReply, but we do not expect one when doing this ! */
                                        stdout.printf(_("Successfully reexecuted the daemon\n"));
                                    else /* Forward critical error */
                                        GLib.critical("%s (%s, %d)", e.message, e.domain.to_string(), e.code);
                                }
                                break;
                            default:
                                gpaste.add(args[1]);
                                break;
                            }
                            break;
                        case 3:
                            switch (args[1]) {
                            case "add":
                                gpaste.add(args[2]);
                                break;
                            case "get":
                                stdout.printf("%s", gpaste.get_element(int.parse(args[2])));
                                break;
                            case "set":
                                gpaste.select(int.parse(args[2]));
                                break;
                            case "delete":
                                gpaste.delete(int.parse(args[2]));
                                break;
                            case "file":
                            case "-f":
                            case "--file":
                                var file = GLib.File.new_for_path(args[2]);
                                try {
                                    var dis = new GLib.DataInputStream(file.read());
                                    var text = new GLib.StringBuilder();
                                    string line = dis.read_line(null);
                                    if (line != null)
                                        text.append(line);
                                    while ((line = dis.read_line(null)) != null)
                                        text.append_c('\n').append(line);
                                    gpaste.add(text.str);
                                } catch(GLib.Error e) {
                                    stderr.printf(_("Could not read file: %s\n"), args[2]);
                                }
                                break;
                            default:
                                app.usage(args[0]);
                                return 1;
                            }
                            break;
                        default:
                            app.usage(args[0]);
                            return 1;
                        }
                    }
                    return 0;
                } catch (IOError e) {
                    stderr.printf(_("Couldn't connect to GPaste daemon.\n"));
                    return 1;
                }
            }
        }

    }

}

