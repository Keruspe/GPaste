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

        /* Deprecated */
        public enum ItemKind {
            TEXT,
            IMAGE
        }

        public abstract class Item : GLib.Object {
            public string? str {
                get;
                protected set;
            }

            public abstract bool has_value ();
            public abstract string get_display_str ();
            public abstract string get_kind ();
            public abstract bool equals (Item i);
        }

        public class TextItem : Item {
            public TextItem (string? str) {
                this.str = str;
            }

            public override string get_display_str () {
                return this.str;
            }

            public override bool has_value () {
                return this.str != null && this.str.strip () != "";
            }

            public override string get_kind () {
                return "Text";
            }

            public override bool equals (Item i) {
                return i is TextItem && i.str == this.str;
            }
        }

        public class ImageItem : Item {
            private string display_str;
            public Gdk.Pixbuf? img {
                get;
                private set;
            }

            public ImageItem (Gdk.Pixbuf? img) {
                this.display_str = "display_str";
                this.str = "path";
                this.img = img;
            }

            public ImageItem.from_path (string path) {
                this.display_str = "display_str";
                this.str = path;
                this.img = null; //TODO
            }

            public override string get_display_str () {
                return this.display_str;
            }

            public override bool has_value () {
                return this.img != null;
            }

            public override string get_kind () {
                return "Image";
            }

            public override bool equals (Item i) {
                return i is ImageItem && i.str == this.str;
            }
        }

    }

}

