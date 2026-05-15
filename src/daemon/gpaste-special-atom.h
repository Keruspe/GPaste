/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#pragma once

#include <gpaste/gpaste-macros.h>

G_BEGIN_DECLS

typedef enum
{
    G_PASTE_SPECIAL_ATOM_FIRST,

    G_PASTE_SPECIAL_ATOM_GNOME_COPIED_FILES = G_PASTE_SPECIAL_ATOM_FIRST,
    G_PASTE_SPECIAL_ATOM_TEXT_HTML,
    G_PASTE_SPECIAL_ATOM_TEXT_HTML_UTF8,
    G_PASTE_SPECIAL_ATOM_TEXT_XML,
    G_PASTE_SPECIAL_ATOM_TEXT_XML_UTF8,

    G_PASTE_SPECIAL_ATOM_LAST,
    G_PASTE_SPECIAL_ATOM_INVALID = -1
} GPasteSpecialAtom;

#define G_PASTE_TYPE_SPECIAL_ATOM (g_paste_special_atom_get_type ())
GType        g_paste_special_atom_get_type (void);

const gchar *g_paste_special_atom_get      (GPasteSpecialAtom atom);

G_END_DECLS
