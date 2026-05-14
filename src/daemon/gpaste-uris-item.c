/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste/gpaste-util.h>

#include <gpaste-uris-item.h>

struct _GPasteUrisItem
{
    GPasteTextItem parent_instance;
};

typedef struct
{
    GdkFileList *file_list;
} GPasteUrisItemPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UrisItem, uris_item, G_PASTE_TYPE_TEXT_ITEM)

/**
 * g_paste_uris_item_get_file_list:
 * @self: a #GPasteUrisItem instance
 *
 * Get the file list contained in the #GPasteUrisItem
 *
 * Returns: (transfer none): read-only #GdkFileList
 */
G_PASTE_VISIBLE GdkFileList *
g_paste_uris_item_get_file_list (const GPasteUrisItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_URIS_ITEM (self), NULL);

    const GPasteUrisItemPrivate *priv = _g_paste_uris_item_get_instance_private (self);

    return priv->file_list;
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
    GPasteUrisItemPrivate *priv = g_paste_uris_item_get_instance_private (G_PASTE_URIS_ITEM (object));

    g_boxed_free (GDK_TYPE_FILE_LIST, priv->file_list);

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

static GPasteItem *
_g_paste_uris_item_new (const gchar *uris_joined,
                        GdkFileList *file_list)
{
    GPasteItem *self = g_paste_item_new (G_PASTE_TYPE_URIS_ITEM, uris_joined);
    GPasteUrisItemPrivate *priv = g_paste_uris_item_get_instance_private (G_PASTE_URIS_ITEM (self));

    g_autofree gchar *display_no_home = g_paste_util_replace (uris_joined, g_get_home_dir (), "~");
    g_autofree gchar *display_flat = g_paste_util_replace (display_no_home, "\n", " ");
    g_autofree gchar *display = g_strconcat (_("[Files] "), display_flat, NULL);
    g_paste_item_set_display_string (self, display);

    guint64 n_uris = g_slist_length (gdk_file_list_get_files (file_list));
    g_paste_item_add_size (self, strlen (uris_joined) + 1 + n_uris);

    priv->file_list = file_list;

    return self;
}

/**
 * g_paste_uris_item_new_from_str:
 * @str: a string containing newline-separated file URIs
 *
 * Create a new instance of #GPasteUrisItem from its string representation
 *
 * Returns: a newly allocated #GPasteUrisItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_uris_item_new_from_str (const gchar *str)
{
    g_return_val_if_fail (str != NULL, NULL);
    g_return_val_if_fail (g_utf8_validate (str, -1, NULL), NULL);

    g_auto (GStrv) uris = g_strsplit (str, "\n", 0);
    guint64 length = g_strv_length (uris);

    if (!length)
        return NULL;

    g_autoslist (GFile) files = NULL;
    for (guint64 i = 0; i < length; ++i)
        files = g_slist_prepend (files, g_file_new_for_uri (uris[i]));
    files = g_slist_reverse (files);

    GdkFileList *file_list = gdk_file_list_new_from_list (files);

    return _g_paste_uris_item_new (str, file_list);
}

/**
 * g_paste_uris_item_new:
 * @file_list: (transfer none): a #GdkFileList from the clipboard
 *
 * Create a new instance of #GPasteUrisItem
 *
 * Returns: a newly allocated #GPasteUrisItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_uris_item_new (GdkFileList *file_list)
{
    g_return_val_if_fail (file_list != NULL, NULL);

    GSList *files = gdk_file_list_get_files (file_list);

    if (!files)
        return NULL;

    g_autoptr (GString) uris_joined = g_string_new (NULL);

    for (GSList *l = files; l; l = l->next)
    {
        g_autofree gchar *uri = g_file_get_uri (G_FILE (l->data));
        if (uris_joined->len > 0)
            g_string_append_c (uris_joined, '\n');
        g_string_append (uris_joined, uri);
    }

    if (!g_utf8_validate (uris_joined->str, -1, NULL))
        return NULL;

    return _g_paste_uris_item_new (uris_joined->str,
                                   g_boxed_copy (GDK_TYPE_FILE_LIST, file_list));
}
