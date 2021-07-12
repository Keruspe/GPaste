/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-uris-item.h>
#include <gpaste-util.h>

struct _GPasteUrisItem
{
    GPasteTextItem parent_instance;
};

typedef struct
{
    GSList *files;
} GPasteUrisItemPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UrisItem, uris_item, G_PASTE_TYPE_TEXT_ITEM)

/**
 * g_paste_uris_item_get_files:
 * @self: a #GPasteUrisItem instance
 *
 * Get the list of files contained in the #GPasteUrisItem
 *
 * Returns: (transfer none) (element-type GFile): #GdkFileList
 */
G_PASTE_VISIBLE const GSList *
g_paste_uris_item_get_files (const GPasteUrisItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_URIS_ITEM (self), NULL);

    const GPasteUrisItemPrivate *priv = _g_paste_uris_item_get_instance_private (self);

    return priv->files;
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

    g_boxed_free (GDK_TYPE_FILE_LIST, (gpointer) priv->files);

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
 * @files: (element-type GFile): a #GdkFileList instance
 *
 * Create a new instance of #GPasteUrisItem
 *
 * Returns: a newly allocated #GPasteUrisItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_uris_item_new (const GSList *files)
{
    g_return_val_if_fail (files, NULL);

    g_autoptr (GString) uris = g_string_new (NULL);
    // This is the prefix displayed in history to identify selected files
    g_autoptr (GString) display_string = g_string_new (_("[Files]"));
    const gchar *home = g_get_home_dir ();

    for (const GSList *f = files; f; f = f->next)
    {
        GFile *file = f->data;
        g_autofree gchar *uri = g_file_get_uri (file);
        g_autofree gchar *path = g_file_get_path (file);
        
        g_string_append_printf (uris, "%s\n", uri);

        if (path)
        {
            g_autofree gchar *path_with_home = g_paste_util_replace (path, home, "~");
            g_string_append_printf (display_string, " %s", path_with_home);
        }
        else
        {
            g_string_append_printf (display_string, " %s", uri);
        }
    }

    GPasteItem *self = g_paste_item_new (G_PASTE_TYPE_URIS_ITEM, uris->str);
    GPasteUrisItemPrivate *priv = g_paste_uris_item_get_instance_private (G_PASTE_URIS_ITEM (self));

    g_paste_item_set_display_string (self, display_string->str);
    priv->files = g_boxed_copy (GDK_TYPE_FILE_LIST, (gconstpointer) files);

    return self;
}
