/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#include <gpaste/gpaste-macros.h>

#include <gtk/gtk.h>

#ifndef __G_PASTE_SPECIAL_ATOM_H__
#define __G_PASTE_SPECIAL_ATOM_H__

G_BEGIN_DECLS

typedef enum
{
    G_PASTE_SPECIAL_ATOM_FIRST,

    G_PASTE_SPECIAL_ATOM_GNOME_COPIED_FILES = G_PASTE_SPECIAL_ATOM_FIRST,
    G_PASTE_SPECIAL_ATOM_TEXT_HTML,
    G_PASTE_SPECIAL_ATOM_TEXT_XML,

    G_PASTE_SPECIAL_ATOM_LAST,
    G_PASTE_SPECIAL_ATOM_INVALID = -1
} GPasteSpecialAtom;

#define G_PASTE_TYPE_SPECIAL_ATOM (g_paste_special_atom_get_type ())
GType g_paste_special_atom_get_type (void);

GdkAtom g_paste_special_atom_get (GPasteSpecialAtom atom);

G_END_DECLS

#endif /*__G_PASTE_SPECIAL_ATOM_H__*/
