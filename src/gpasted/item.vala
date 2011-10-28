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

        public enum ItemKind {
            STRING,
            IMAGE
        }

        public struct Item {
            public ItemKind kind;

            public string? str;
            public Gdk.Pixbuf? img;

            public Item.string (string? str) {
                this.kind = ItemKind.STRING;
                this.str = str;
                this.img = null;
            }

            public Item.image (Gdk.Pixbuf? img) {
                this.kind = ItemKind.IMAGE;
                this.str = "image";
                this.img = img;
            }

            public bool has_value () {
                switch (this.kind) {
                case ItemKind.STRING:
                    return (this.str != null && this.str.strip() != "");
                case ItemKind.IMAGE:
                    return (this.img != null);
                }
                return false;
            }
        }

    }

}

