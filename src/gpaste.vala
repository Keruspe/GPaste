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
    interface GPasteBusClient : Object {
        [DBus (signature = "as")]
        public abstract Variant getHistory() throws IOError;
        public abstract void add(string selection) throws IOError;
        public abstract void select (uint index) throws IOError;
    }

    public class GPaste : Object {
        public static void usage(string caller) {
            stdout.printf(_("Usage:\n"));
            stdout.printf(_(caller + ": print the history\n"));
            stdout.printf(_(caller + " <text>: set text to clipboard\n"));
            stdout.printf(_(caller + "set <number>: set <number>th item in history to clipboard\n"));
            stdout.printf(_(caller + "delete <number>: delete <number>th item in history\n"));
            stdout.printf(_("whatever | " + caller + ": set the output of whatever to clipboard\n"));
        }

        public static int main(string[] args) {
            try {
                GPasteBusClient gpaste = Bus.get_proxy_sync(BusType.SESSION, "org.gnome.GPaste", "/org/gnome/GPaste");
                if (! Posix.isatty(stdin.fileno())) {
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
                        string[] history = (string[]) gpaste.getHistory();
                        for (int i = 0 ; i < history.length ; ++i)
                            stdout.printf("%d: %s\n", i, history[i]);
                        break;
                    case 2:
                        if (args[1] == "help")
                            usage(args[0]);
                        else
                            gpaste.add(args[1]);
                        break;
                    case 3:
                        switch (args[1]) {
                        case "set":
                            gpaste.select(args[2].to_int());
                            break;
                        case "delete"
                            gpaste.delete(args[2].to_int());
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
                stderr.printf("%s\n", e.message);
                return 1;
            }
        }
    }

}
