// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-special-mime.h>

static const gchar *special_mimes[G_PASTE_SPECIAL_MIME_LAST] = {
    [G_PASTE_SPECIAL_MIME_GNOME_COPIED_FILES] = "x-special/gnome-copied-files",
    [G_PASTE_SPECIAL_MIME_TEXT_HTML]          = "text/html",
    [G_PASTE_SPECIAL_MIME_TEXT_HTML_UTF8]     = "text/html;charset=utf-8",
    [G_PASTE_SPECIAL_MIME_TEXT_XML]           = "text/xml",
    [G_PASTE_SPECIAL_MIME_TEXT_XML_UTF8]      = "text/xml;charset=utf-8",
};

G_PASTE_VISIBLE GType
g_paste_special_mime_get_type (void)
{
    static GType etype = 0;

    if (!etype)
    {
        static const GEnumValue values[] = {
            { G_PASTE_SPECIAL_MIME_GNOME_COPIED_FILES, "G_PASTE_SPECIAL_MIME_GNOME_COPIED_FILES", "gnome-copied-files" },
            { G_PASTE_SPECIAL_MIME_TEXT_HTML,          "G_PASTE_SPECIAL_MIME_TEXT_HTML",          "text-html"          },
            { G_PASTE_SPECIAL_MIME_TEXT_HTML_UTF8,     "G_PASTE_SPECIAL_MIME_TEXT_HTML_UTF8",     "text-html-utf8"     },
            { G_PASTE_SPECIAL_MIME_TEXT_XML,           "G_PASTE_SPECIAL_MIME_TEXT_XML",           "text-xml"           },
            { G_PASTE_SPECIAL_MIME_TEXT_XML_UTF8,      "G_PASTE_SPECIAL_MIME_TEXT_XML_UTF8",      "text-xml-utf8"      },
            { G_PASTE_SPECIAL_MIME_INVALID,            NULL,                                      NULL                 },
        };
        etype = g_enum_register_static (g_intern_static_string ("GPasteSpecialMime"), values);
        g_type_class_ref (etype);
    }

    return etype;
}

/**
 * g_paste_special_mime_get:
 * @mime: the representation we want the mime type of
 *
 * Find special MIME type strings handled by GPaste
 *
 * Returns: the MIME type string corresponding to @mime
 */
G_PASTE_VISIBLE const gchar *
g_paste_special_mime_get (GPasteSpecialMime mime)
{
    g_return_val_if_fail (mime >= G_PASTE_SPECIAL_MIME_FIRST && mime < G_PASTE_SPECIAL_MIME_LAST, NULL);

    return special_mimes[mime];
}
