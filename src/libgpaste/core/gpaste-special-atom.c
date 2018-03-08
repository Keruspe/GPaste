/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-special-atom.h>

static GdkAtom special_atoms[G_PASTE_SPECIAL_ATOM_LAST] = { 0 };

static void
g_paste_special_atom_init(void)
{
    static gboolean inited = FALSE;

    if (!inited)
    {
        g_debug("atoms init");
        special_atoms[G_PASTE_SPECIAL_ATOM_GNOME_COPIED_FILES] = gdk_atom_intern_static_string ("x-special/gnome-copied-files");
        special_atoms[G_PASTE_SPECIAL_ATOM_TEXT_HTML]          = gdk_atom_intern_static_string ("text/html");
        special_atoms[G_PASTE_SPECIAL_ATOM_TEXT_XML]           = gdk_atom_intern_static_string ("text/xml");
        inited = TRUE;
    }
}

G_PASTE_VISIBLE GType
g_paste_special_atom_get_type (void)
{
    static GType etype = 0;
    if (!etype)
    {
        g_paste_special_atom_init ();
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
 * g_paste_special_atom_get
 * @atom: the atom we want to get
 * 
 * Find special atoms handled by GPaste
 *
 * Returns: (transfer none): the atom corresponding to @atom
 */
G_PASTE_VISIBLE GdkAtom
g_paste_special_atom_get (GPasteSpecialAtom atom)
{
    g_paste_special_atom_init ();
    return special_atoms[atom];
}
