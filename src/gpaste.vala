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
            } else if (args.length == 1) {
                string[] history = (string[]) gpaste.getHistory();
                for (int i = 0 ; i < history.length ; ++i)
                    stdout.printf("%d: %s\n", i, history[i]);
            } else if (args.length == 3 && args[1] == "set") {
                gpaste.select(args[2].to_int());
            } else {
                gpaste.add(args[1]);
            }
        } catch (IOError e) {
            stderr.printf("%s\n", e.message);
        }
        return 0;
    }

}
