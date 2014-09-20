/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include <gpaste-update-enums.h>

G_PASTE_VISIBLE GType
g_paste_update_action_get_type (void)
{
    static GType etype = 0;
    if (!etype)
    {
        static const GEnumValue values[] = {
            { G_PASTE_UPDATE_ACTION_REPLACE, "G_PASTE_UPDATE_ACTION_REPLACE", "REPLACE" },
            { G_PASTE_UPDATE_ACTION_REMOVE,  "G_PASTE_UPDATE_ACTION_REMOVE",  "REMOVE"  },
            { G_PASTE_UPDATE_ACTION_INVALID, NULL,                            NULL      }
        };
        etype = g_enum_register_static (g_intern_static_string ("GPasteUpdateAction"), values);
        g_type_class_ref (etype);
    }
    return etype;
}

G_PASTE_VISIBLE GType
g_paste_update_target_get_type (void)
{
    static GType etype = 0;
    if (!etype)
    {
        static const GEnumValue values[] = {
            { G_PASTE_UPDATE_TARGET_ALL,      "G_PASTE_UPDATE_TARGET_ALL",      "ALL"      },
            { G_PASTE_UPDATE_TARGET_POSITION, "G_PASTE_UPDATE_TARGET_POSITION", "POSITION" },
            { G_PASTE_UPDATE_TARGET_INVALID,  NULL,                             NULL       }
        };
        etype = g_enum_register_static (g_intern_static_string ("GPasteUpdateTarget"), values);
        g_type_class_ref (etype);
    }
    return etype;
}
