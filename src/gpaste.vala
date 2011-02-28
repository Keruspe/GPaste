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

    namespace Client {

        [DBus (name = "org.gnome.GPaste")]
        interface DBusClient : GLib.Object {
            [DBus (name = "GetHistory", inSignature = "", outSignature = "as")]
            public abstract GLib.Variant getHistory() throws IOError;
            [DBus (name = "Add", inSignature = "s", outSignature = "")]
            public abstract void add(string selection) throws IOError;
            [DBus (name = "Select", inSignature = "u", outSignature = "")]
            public abstract void select(uint32 index) throws IOError;
            [DBus (name = "Delete", inSignature = "u", outSignature = "")]
            public abstract void delete(uint32 index) throws IOError;
            [DBus (name = "Empty", inSignature = "", outSignature = "")]
            public abstract void empty() throws IOError;
            [DBus (name = "Launch", inSignature = "", outSignature = "")]
            public abstract void launch() throws IOError;
            [DBus (name = "Stop", inSignature = "", outSignature = "")]
            public abstract void stop() throws IOError;
        }

        public class Main : GLib.Object {
            public static void usage(string caller) {
                stdout.printf(_("Usage:\n"));
                stdout.printf(_("%s: print the history\n"), caller);
                stdout.printf(_("%s [add] <text>: set text to clipboard\n"), caller);
                stdout.printf(_("%s set <number>: set <number>th item of the history to clipboard\n"), caller);
                stdout.printf(_("%s delete <number>: delete <number>th item of the history\n"), caller);
                stdout.printf(_("whatever | %s: set the output of whatever to clipboard\n"), caller);
                stdout.printf(_("%s empty: empty the history\n"), caller);
                stdout.printf(_("%s stop: shutdown the daemon\n"), caller);
                stdout.printf(_("%s quit: alias for quit\n"), caller);
                stdout.printf(_("%s applet: launch the applet\n"), caller);
                stdout.printf(_("%s settings: launch the configuration tool\n"), caller);
                stdout.printf(_("%s version: display the version\n"), caller);
                stdout.printf(_("%s help: display this help\n"), caller);
            }

            public static int main(string[] args) {
                GLib.Intl.bindtextdomain(Config.GETTEXT_PACKAGE, Config.LOCALEDIR);
                GLib.Intl.bind_textdomain_codeset(Config.GETTEXT_PACKAGE, "UTF-8");
                GLib.Intl.textdomain(Config.GETTEXT_PACKAGE);
                GLib.Intl.setlocale(LocaleCategory.ALL, "");
                try {
                    DBusClient gpaste = Bus.get_proxy_sync(BusType.SESSION, "org.gnome.GPaste", "/org/gnome/GPaste");
                    if (! Posix.isatty(stdin.fileno())) {
                        /* We are being piped ! */
                        var sb = new StringBuilder();
                        sb.append(stdin.read_line());
                        string s;
                        while ((s = stdin.read_line()) != null) {
                            sb.append_c('\n');
                            sb.append(s);
                        }
                        gpaste.add(sb.str);
                    } else {
                        switch (args.length) {
                        case 1:
                            var history = gpaste.getHistory() as string[];
                            for (int i = 0 ; i < history.length ; ++i)
                                stdout.printf("%d: %s\n", i, history[i]);
                            break;
                        case 2:
                            switch (args[1]) {
                            case "help":
                            case "-h":
                            case "--help":
                                usage(args[0]);
                                break;
                            case "start":
                            case "daemon":
                                gpaste.launch();
                                break;
                            case "stop":
                            case "quit":
                                gpaste.stop();
                                break;
                            case "empty":
                                gpaste.empty();
                                break;
                            case "version":
                                stdout.printf(Config.PACKAGE_STRING+"\n");
                                break;
                            case "applet":
                                try {
                                    GLib.Process.spawn_command_line_async(Config.PKGLIBEXECDIR + "/gpaste-applet");
                                } catch(SpawnError e) {
                                    stderr.printf(_("Couldn't spawn gpaste-applet.\n"));
                                }
                                break;
                            case "settings":
                            case "preferences":
                                Posix.execl(Config.PKGLIBEXECDIR + "/gpaste-settings", "GPaste-Settings");
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
                            case "set":
                                gpaste.select(int.parse(args[2]));
                                break;
                            case "delete":
                                gpaste.delete(int.parse(args[2]));
                                break;
                            default:
                                usage(args[0]);
                                return 1;
                            }
                            break;
                        default:
                            usage(args[0]);
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
