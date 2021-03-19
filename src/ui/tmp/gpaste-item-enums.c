/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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
