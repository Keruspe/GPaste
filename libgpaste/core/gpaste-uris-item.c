/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gpaste-uris-item-private.h"

#include <glib/gi18n-lib.h>

struct _GPasteUrisItemPrivate
{
    GStrv uris;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUrisItem, g_paste_uris_item, G_PASTE_TYPE_TEXT_ITEM)

/**
 * g_paste_uris_item_get_uris:
 * @self: a #GPasteUrisItem instance
 *
 * Get the list of uris contained in the #GPasteUrisItem
 *
 * Returns: (transfer none): read-only array of read-only uris (strings)
 */
G_PASTE_VISIBLE const gchar * const *
g_paste_uris_item_get_uris (const GPasteUrisItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_URIS_ITEM (self), FALSE);

    GPasteUrisItemPrivate *priv = g_paste_uris_item_get_instance_private ((GPasteUrisItem *) self);

    return (const gchar * const *) priv->uris;
}

static gboolean
g_paste_uris_item_equals (const GPasteItem *self,
                          const GPasteItem *other)
{
    return (G_PASTE_IS_URIS_ITEM (other) &&
            G_PASTE_ITEM_CLASS (g_paste_uris_item_parent_class)->equals (self, other));
}



static const gchar *
g_paste_uris_item_get_kind (const GPasteItem *self G_GNUC_UNUSED)
{
    return "Uris";
}

static void
g_paste_uris_item_finalize (GObject *object)
{
    GPasteUrisItemPrivate *priv = g_paste_uris_item_get_instance_private (G_PASTE_URIS_ITEM (object));

    g_strfreev (priv->uris);

    G_OBJECT_CLASS (g_paste_uris_item_parent_class)->finalize (object);
}

static void
g_paste_uris_item_class_init (GPasteUrisItemClass *klass)
{
    GPasteItemClass *item_class = G_PASTE_ITEM_CLASS (klass);

    item_class->equals = g_paste_uris_item_equals;
    item_class->get_kind = g_paste_uris_item_get_kind;

    G_OBJECT_CLASS (klass)->finalize = g_paste_uris_item_finalize;
}

static void
g_paste_uris_item_init (GPasteUrisItem *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_uris_item_new:
 * @uris: a string containing the paths separated by "\n" (as returned by gtk_clipboard_wait_for_uris)
 *
 * Create a new instance of #GPasteUrisItem
 *
 * Returns: a newly allocated #GPasteUrisItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_uris_item_new (const gchar *uris)
{
    g_return_val_if_fail (uris, NULL);
    g_return_val_if_fail (g_utf8_validate (uris, -1, NULL), NULL);

    GPasteItem *self = g_paste_item_new (G_PASTE_TYPE_URIS_ITEM, uris);
    GPasteUrisItemPrivate *priv = g_paste_uris_item_get_instance_private (G_PASTE_URIS_ITEM (self));

    G_PASTE_CLEANUP_FREE gchar *home_escaped = g_regex_escape_string (g_get_home_dir (), -1);
    G_PASTE_CLEANUP_REGEX_UNREF GRegex *regex = g_regex_new (home_escaped,
                                                             0, /* Compile options */
                                                             0, /* Match options */
                                                             NULL); /* Error */
    G_PASTE_CLEANUP_FREE gchar *display_string_with_newlines = g_regex_replace_literal (regex,
                                                                                        uris,
                                                                                        (gssize) -1,
                                                                                        0, /* Start position */
                                                                                        "~",
                                                                                        0, /* Match options */
                                                                                        NULL); /* Error */
    G_PASTE_CLEANUP_REGEX_UNREF GRegex *_regex = g_regex_new ("\\n",
                                                              0, /* Compile options */
                                                              0, /* Match options */
                                                              NULL); /* Error */
    G_PASTE_CLEANUP_FREE gchar *display_string = g_regex_replace_literal (_regex,
                                                                          display_string_with_newlines,
                                                                          (gssize) -1,
                                                                          0, /* Start position */
                                                                          " ",
                                                                          0, /* Match options */
                                                                          NULL); /* Error */

    // This is the prefix displayed in history to identify selected files
    G_PASTE_CLEANUP_FREE gchar *full_display_string = g_strconcat (_("[Files] "), display_string, NULL);

    g_paste_item_set_display_string (self, full_display_string);

    G_PASTE_CLEANUP_STRFREEV GStrv paths = g_strsplit (uris, "\n", 0);
    guint length = g_strv_length (paths);

    self->size += length + 1;

    GStrv _uris = priv->uris = g_new (gchar *, length + 1);
    for (guint i = 0; i < length; ++i)
    {
        _uris[i] = g_strconcat ("file://", paths[i], NULL);
        self->size += strlen (_uris[i]) + 1;
    }
    _uris[length] = NULL;

    return self;
}
