// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-macros.h>

G_BEGIN_DECLS

/* The mime types a clipboard owner offers to say that what it is offering is a
 * secret. Each is a key with the values it may be said under; the payload stays
 * the ordinary text alongside, which is why these are a list of their own rather
 * than more #GPasteSpecialMime: nothing is kept under them and nothing is drawn
 * from them. They are published for a password item and read back to recognise
 * one, and that is all.
 *
 * A backend therefore reads them on any text update, whatever rich-text support
 * says about the special values beside them: what they carry is not a richer
 * representation of the content but a fact about it, and one that decides what
 * the item is rather than what else it holds.
 *
 * No INVALID member, unlike #GPasteSpecialMime: that one is parsed back off
 * disk and so needs a value standing for a name nothing matched, where nothing
 * is ever stored under one of these. A -1 member would only widen the
 * enumeration to a signed type and make every bound check below carry a lower
 * half that cannot fail. */
typedef enum
{
    G_PASTE_SENSITIVE_MIME_FIRST,

    G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT = G_PASTE_SENSITIVE_MIME_FIRST,

    G_PASTE_SENSITIVE_MIME_LAST
} GPasteSensitiveMime;

const gchar *g_paste_sensitive_mime_get       (GPasteSensitiveMime mime);
GBytes      *g_paste_sensitive_mime_get_bytes (GPasteSensitiveMime mime);
gboolean     g_paste_sensitive_mime_matches   (GPasteSensitiveMime mime,
                                               GBytes             *bytes);

G_END_DECLS
