// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-macros.h>

G_BEGIN_DECLS

typedef enum
{
    G_PASTE_SPECIAL_MIME_FIRST,

    G_PASTE_SPECIAL_MIME_GNOME_COPIED_FILES = G_PASTE_SPECIAL_MIME_FIRST,
    G_PASTE_SPECIAL_MIME_TEXT_HTML,
    G_PASTE_SPECIAL_MIME_TEXT_HTML_UTF8,
    G_PASTE_SPECIAL_MIME_TEXT_XML,
    G_PASTE_SPECIAL_MIME_TEXT_XML_UTF8,

    G_PASTE_SPECIAL_MIME_LAST,
    G_PASTE_SPECIAL_MIME_INVALID = -1
} GPasteSpecialMime;

#define G_PASTE_TYPE_SPECIAL_MIME (g_paste_special_mime_get_type ())
GType        g_paste_special_mime_get_type (void);

const gchar *g_paste_special_mime_get      (GPasteSpecialMime mime);

G_END_DECLS
