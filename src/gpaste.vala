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

    namespace Client {

        [DBus (name = "org.gnome.GPaste")]
        interface DBusClient : GLib.Object {
            [DBus (name = "GetHistory", inSignature = "", outSignature = "as")]
            public abstract string[] get_history() throws GLib.IOError;
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
            private DBusClient gpaste;

            private void usage(string caller) {
                stdout.printf(_("Usage:\n"));
                stdout.printf(_("%s [history]: print the history with indexes\n"), caller);
                stdout.printf(_("%s raw-history: print the history without indexes\n"), caller);
                stdout.printf(_("%s zero-history: print the history with NUL as separator\n"), caller);
                stdout.printf(_("%s add <text>: set text to clipboard\n"), caller);
                stdout.printf(_("%s get <number>: get the <number>th item from the history\n"), caller);
                stdout.printf(_("%s set <number>: set the <number>th item from the history to the clipboard\n"), caller);
                stdout.printf(_("%s delete <number>: delete <number>th item of the history\n"), caller);
                stdout.printf(_("%s file <path>: put the content of the file at <path> into the clipboard\n"), caller);
                stdout.printf(_("whatever | %s: set the output of whatever to clipboard\n"), caller);
                stdout.printf(_("%s empty: empty the history\n"), caller);
                stdout.printf(_("%s start: start tracking clipboard changes\n"), caller);
                stdout.printf(_("%s stop: stop tracking clipboard changes\n"), caller);
                stdout.printf(_("%s quit: alias for stop\n"), caller);
                stdout.printf(_("%s daemon-reexec: reexecute the daemon (after upgrading...)\n"), caller);
                stdout.printf(_("%s settings: launch the configuration tool\n"), caller);
                stdout.printf(_("%s version: display the version\n"), caller);
                stdout.printf(_("%s help: display this help\n"), caller);
            }

            private void history(bool raw = false, bool zero = false) throws GLib.IOError {
                var history = this.gpaste.get_history();
                for (int i = 0 ; i < history.length ; ++i) {
                    if (!raw)
                        stdout.printf("%d: ", i);
                    stdout.printf("%s", history[i]);
                    if (zero)
                        stdout.putc('\0');
                    else
                        stdout.putc('\n');
                }
            }

            public static int main(string[] args) {
                const string gettext_package = Config.GETTEXT_PACKAGE;
                GLib.Intl.bindtextdomain(gettext_package, Config.LOCALEDIR);
                GLib.Intl.bind_textdomain_codeset(gettext_package, "UTF-8");
                GLib.Intl.textdomain(gettext_package);
                GLib.Intl.setlocale(LocaleCategory.ALL, "");
                var app = new Main();
                try {
                    app.gpaste = Bus.get_proxy_sync(BusType.SESSION, "org.gnome.GPaste", "/org/gnome/GPaste");
                    if (!Posix.isatty(stdin.fileno())) {
                        /* We are being piped ! */
                        var text = new StringBuilder();
                        string line = stdin.read_line();
                        if (line != null)
                            text.append(line);
                        while ((line = stdin.read_line()) != null)
                            text.append_c('\n').append(line);
                        app.gpaste.add(text.str);
                    } else {
                        switch (args.length) {
                        case 1:
                            app.history();
                            break;
                        case 2:
                            switch (args[1]) {
                            case "help":
                            case "-h":
                            case "--help":
                                app.usage(args[0]);
                                break;
                            case "start":
                            case "d":
                            case "daemon":
                                app.gpaste.track(true);
                                break;
                            case "stop":
                            case "q":
                            case "quit":
                                app.gpaste.track(false);
                                break;
                            case "e":
                            case "empty":
                                app.gpaste.empty();
                                break;
                            case "v":
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
                            case "s":
                            case "settings":
                            case "p":
                            case "preferences":
                                Posix.execl(Config.PKGLIBEXECDIR + "/gpaste-settings", "GPaste-Settings");
                                break;
                            case "dr":
                            case "daemon-reexec":
                                try {
                                    app.gpaste.reexec();
                                } catch (GLib.Error e) {
                                    if (e.code == 4) /* NoReply, but we do not expect one when doing this ! */
                                        stdout.printf(_("Successfully reexecuted the daemon\n"));
                                    else /* Forward critical error */
                                        GLib.critical("%s (%s, %d)", e.message, e.domain.to_string(), e.code);
                                }
                                break;
                            case "h":
                            case "history":
                                app.history();
                                break;
                            case "rh":
                            case "raw-history":
                                app.history(true);
                                break;
                            case "zh":
                            case "zero-history":
                                app.history (false, true);
                                break;
                            default:
                                app.usage(args[0]);
                                break;
                            }
                            break;
                        case 3:
                            switch (args[1]) {
                            case "a":
                            case "add":
                                app.gpaste.add(args[2]);
                                break;
                            case "g":
                            case "get":
                                stdout.printf("%s", app.gpaste.get_element(int.parse(args[2])));
                                break;
                            case "s":
                            case "set":
                                app.gpaste.select(int.parse(args[2]));
                                break;
                            case "d":
                            case "delete":
                                app.gpaste.delete(int.parse(args[2]));
                                break;
                            case "-f":
                            case "--file":
                                stderr.printf(_("%s %s is deprecated: use \"%s file\" instead\n"), args[0], args[1], args[0]);
                                break;
                            case "f":
                            case "file":
                                var file = GLib.File.new_for_path(args[2]);
                                try {
                                    var dis = new GLib.DataInputStream(file.read());
                                    var text = new GLib.StringBuilder();
                                    string line = dis.read_line(null);
                                    if (line != null)
                                        text.append(line);
                                    while ((line = dis.read_line(null)) != null)
                                        text.append_c('\n').append(line);
                                    app.gpaste.add(text.str);
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
                    /* Display help even if we cannot contact the daemon */
                    if (args.length == 2) {
                        switch (args[1]) {
                        case "help":
                        case "-h":
                        case "--help":
                            app.usage(args[0]);
                            break;
                        default:
                            break;
                        }
                        return 0;

                    }
                    stderr.printf(_("Couldn't connect to GPaste daemon.\n"));
                    return 1;
                }
            }
        }

    }

}

