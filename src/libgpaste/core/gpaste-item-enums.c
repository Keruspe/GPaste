/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include <gpaste-macros.h>
#include <gpaste-item-enums.h>

G_PASTE_VISIBLE GType
g_paste_item_kind_get_type (void)
{
    static GType etype = 0;
    if (!etype)
    {
        static const GEnumValue values[] = {
            { G_PASTE_ITEM_KIND_TEXT,     "G_PASTE_ITEM_KIND_TEXT",     "Text"     },
            { G_PASTE_ITEM_KIND_URIS,     "G_PASTE_ITEM_KIND_URIS",     "Uris"     },
            { G_PASTE_ITEM_KIND_IMAGE,    "G_PASTE_ITEM_KIND_IMAGE",    "Image"    },
            { G_PASTE_ITEM_KIND_PASSWORD, "G_PASTE_ITEM_KIND_PASSWORD", "Password" },
            { G_PASTE_ITEM_KIND_INVALID,  NULL,                          NULL      }
        };
        etype = g_enum_register_static (g_intern_static_string ("GPasteItemKind"), values);
        g_type_class_ref (etype);
    }
    return etype;
}
