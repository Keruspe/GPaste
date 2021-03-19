/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-macros.h>
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
