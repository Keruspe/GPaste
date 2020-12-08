/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2021, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#include <gpaste-macros.h>

#ifndef __G_PASTE_SPECIAL_MIME_H__
#define __G_PASTE_SPECIAL_MIME_H__

G_BEGIN_DECLS

typedef enum
{
    G_PASTE_SPECIAL_MIME_FIRST,

    G_PASTE_SPECIAL_MIME_TEXT_HTML = G_PASTE_SPECIAL_MIME_FIRST,
    G_PASTE_SPECIAL_MIME_TEXT_XML,

    G_PASTE_SPECIAL_MIME_LAST,
    G_PASTE_SPECIAL_MIME_INVALID = -1
} GPasteSpecialMime;

const gchar *g_paste_special_mime_get (GPasteSpecialMime mime);

G_END_DECLS

#endif /*__G_PASTE_SPECIAL_MIME_H__*/
