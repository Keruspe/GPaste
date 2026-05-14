/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-special-atom.h>

static const gchar *special_atoms[G_PASTE_SPECIAL_ATOM_LAST] = {
    [G_PASTE_SPECIAL_ATOM_GNOME_COPIED_FILES] = "x-special/gnome-copied-files",
    [G_PASTE_SPECIAL_ATOM_TEXT_HTML]          = "text/html",
    [G_PASTE_SPECIAL_ATOM_TEXT_XML]           = "text/xml",
};

G_PASTE_VISIBLE GType
g_paste_special_atom_get_type (void)
{
    static GType etype = 0;

    if (!etype)
    {
        static const GEnumValue values[] = {
            { G_PASTE_SPECIAL_ATOM_GNOME_COPIED_FILES, "G_PASTE_SPECIAL_ATOM_GNOME_COPIED_FILES", "gnome-copied-files" },
            { G_PASTE_SPECIAL_ATOM_TEXT_HTML,          "G_PASTE_SPECIAL_ATOM_TEXT_HTML",          "text-html"          },
            { G_PASTE_SPECIAL_ATOM_TEXT_XML,           "G_PASTE_SPECIAL_ATOM_TEXT_XML",           "text-xml"           },
            { G_PASTE_SPECIAL_ATOM_INVALID,            NULL,                                      NULL                 },
        };
        etype = g_enum_register_static (g_intern_static_string ("GPasteSpecialAtom"), values);
        g_type_class_ref (etype);
    }

    return etype;
}

/**
 * g_paste_special_atom_get:
 * @atom: the atom we want to get
 *
 * Find special MIME type strings handled by GPaste
 *
 * Returns: the MIME type string corresponding to @atom
 */
G_PASTE_VISIBLE const gchar *
g_paste_special_atom_get (GPasteSpecialAtom atom)
{
    g_return_val_if_fail (atom >= G_PASTE_SPECIAL_ATOM_FIRST && atom < G_PASTE_SPECIAL_ATOM_LAST, NULL);

    return special_atoms[atom];
}
