/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2021, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-special-mime.h>

#include <gdk/gdk.h>

static const gchar *special_mime[G_PASTE_SPECIAL_MIME_LAST] = { NULL };

static void
g_paste_special_mime_init(void)
{
    static gboolean inited = FALSE;

    if (!inited)
    {
        g_debug("special mime init");
        special_mime[G_PASTE_SPECIAL_MIME_TEXT_HTML] = gdk_intern_mime_type ("text/html");
        special_mime[G_PASTE_SPECIAL_MIME_TEXT_XML]  = gdk_intern_mime_type ("text/xml");
        inited = TRUE;
    }
}

/**
 * g_paste_special_mime_get
 * @mime: the mime type we want to get
 * 
 * Find special mime handled by GPaste
 *
 * Returns: the mime type corresponding to @mime
 */
G_PASTE_VISIBLE const gchar *
g_paste_special_mime_get (GPasteSpecialMime mime)
{
    g_return_val_if_fail (mime >= G_PASTE_SPECIAL_MIME_FIRST && mime < G_PASTE_SPECIAL_MIME_LAST, NULL);
    g_paste_special_mime_init ();
    return special_mime[mime];
}
