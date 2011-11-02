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

            public GLib.DateTime date {
                get;
                private set;
            }

            public string checksum {
                get;
                private set;
            }

            public Gdk.Pixbuf? img {
                get;
                private set;
            }

            public ImageItem (Gdk.Pixbuf? img) {
                this.date = new GLib.DateTime.now_local ();
                this.img = img;
                if (img == null)
                    this.display_str = _("[Image no longer exists]");
                else {
                    string images_dir = GLib.Path.build_filename (GLib.Environment.get_user_data_dir(), "gpaste", "images");
                    if (!GLib.File.new_for_path(images_dir).query_exists())
                        Posix.mkdir(images_dir, 0700);
                    this.checksum = GLib.Checksum.compute_for_data (GLib.ChecksumType.SHA256, (uchar[]) img.get_pixels ());
                    this.str = GLib.Path.build_filename (images_dir, this.checksum + ".png");
                    try {
                        img.save (this.str, "png");
                        this.display_str = _("[Image, %d x %d (%s)]").printf (this.img.get_width (), this.img.get_height (), this.date.format (_("%m/%d/%y %T")));
                    } catch (GLib.Error e) {
                        this.display_str = _("[Image no longer exists]");
                        stderr.printf (_("Error while saving pixbuf: %s\n"), e.message);
                    }
                }
            }

            public ImageItem.load (string path, GLib.DateTime date) {
                this.date = date;
                this.str = path;
                try {
                    this.img = new Gdk.Pixbuf.from_file (this.str);
                    this.display_str = _("[Image, %d x %d (%s)]").printf (this.img.get_width (), this.img.get_height (), this.date.format (_("%m/%d/%y %T")));
                } catch (GLib.Error e) {
                    this.display_str = _("[Image no longer exists]");
                    stderr.printf (_("Error while loading pixbuf: %s\n"), e.message);
                }
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
                return i is ImageItem && (i as ImageItem).checksum == this.checksum;
            }
        }

    }

}

