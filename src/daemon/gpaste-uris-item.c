/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste/gpaste-util.h>

#include <gpaste-uris-item.h>

struct _GPasteUrisItem
{
    GPasteTextItem parent_instance;
};

typedef struct
{
    GStrv uris;
} GPasteUrisItemPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UrisItem, uris_item, G_PASTE_TYPE_TEXT_ITEM)

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
    g_return_val_if_fail (_G_PASTE_IS_URIS_ITEM (self), FALSE);

    const GPasteUrisItemPrivate *priv = _g_paste_uris_item_get_instance_private (self);

    return (const gchar * const *) priv->uris;
}

static gboolean
g_paste_uris_item_equals (const GPasteItem *self,
                          const GPasteItem *other)
{
    return (_G_PASTE_IS_URIS_ITEM (other) &&
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
    const GPasteUrisItemPrivate *priv = _g_paste_uris_item_get_instance_private (G_PASTE_URIS_ITEM (object));

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

    g_autofree gchar *display_string_with_newlines = g_paste_util_replace (uris, g_get_home_dir (), "~");
    g_autofree gchar *display_string = g_paste_util_replace (display_string_with_newlines, "\n", " ");

    // This is the prefix displayed in history to identify selected files
    g_autofree gchar *full_display_string = g_strconcat (_("[Files] "), display_string, NULL);

    g_paste_item_set_display_string (self, full_display_string);

    g_auto (GStrv) paths = g_strsplit (uris, "\n", 0);
    guint64 length = g_strv_length (paths);

    g_paste_item_add_size (self, length + 1);

    GStrv _uris = priv->uris = g_new (gchar *, length + 1);
    for (guint64 i = 0; i < length; ++i)
    {
        _uris[i] = g_strconcat ("file://", paths[i], NULL);
        g_paste_item_add_size (self, strlen (_uris[i]) + 1);
    }
    _uris[length] = NULL;

    return self;
}
