/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk3/gpaste-gtk-util.h>

#include <gpaste-image-item.h>

#include <string.h>
#include <sys/stat.h>

struct _GPasteImageItem
{
    GPasteItem parent_instance;
};

typedef struct _GPasteImageItemPrivate
{
    gchar     *checksum;
    GDateTime *date;
    GdkPixbuf *image;

    guint64    additional_size;
} GPasteImageItemPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (ImageItem, image_item, G_PASTE_TYPE_ITEM)

/**
 * g_paste_image_item_get_checksum:
 * @self: a #GPasteImageItem instance
 *
 * Get the checksum of the GdkPixbuf contained in the #GPasteImageItem
 *
 * Returns: read-only string representatig the SHA256 checksum of the image
 */
G_PASTE_VISIBLE const gchar *
g_paste_image_item_get_checksum (const GPasteImageItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_IMAGE_ITEM (self), NULL);

    const GPasteImageItemPrivate *priv = _g_paste_image_item_get_instance_private (self);

    return priv->checksum;
}

/**
 * g_paste_image_item_get_date:
 * @self: a #GPasteImageItem instance
 *
 * Get the date at which the image was created
 *
 * Returns: read-only GDateTime containing the image's creation date
 */
G_PASTE_VISIBLE const GDateTime *
g_paste_image_item_get_date (const GPasteImageItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_IMAGE_ITEM (self), NULL);

    const GPasteImageItemPrivate *priv = _g_paste_image_item_get_instance_private (self);

    return priv->date;
}

/**
 * g_paste_image_item_get_image:
 * @self: a #GPasteImageItem instance
 *
 * Get the image contained in the #GPasteImageItem
 *
 * Returns: (transfer none): the GdkPixbuf of the image
 */
G_PASTE_VISIBLE GdkPixbuf *
g_paste_image_item_get_image (const GPasteImageItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_IMAGE_ITEM (self), NULL);

    const GPasteImageItemPrivate *priv = _g_paste_image_item_get_instance_private (self);

    return priv->image;
}

G_PASTE_VISIBLE gboolean
g_paste_image_item_is_growing (const GPasteImageItem *self,
                               const GPasteImageItem *other)
{
    g_return_val_if_fail (_G_PASTE_IS_IMAGE_ITEM (self), FALSE);
    g_return_val_if_fail (_G_PASTE_IS_IMAGE_ITEM (other), FALSE);

    const GPasteImageItemPrivate *priv = _g_paste_image_item_get_instance_private (self);
    const GPasteImageItemPrivate *_priv = _g_paste_image_item_get_instance_private (other);

    if (!priv->image || !_priv->image)
        return FALSE;

    gsize len = MIN (gdk_pixbuf_get_byte_length (priv->image), gdk_pixbuf_get_byte_length (_priv->image));

    return !memcmp (gdk_pixbuf_read_pixels (priv->image), gdk_pixbuf_read_pixels (_priv->image), len);
}

static gboolean
g_paste_image_item_equals (const GPasteItem *self,
                           const GPasteItem *other)
{
    if (!_G_PASTE_IS_IMAGE_ITEM (other))
        return FALSE;

    const GPasteImageItemPrivate *priv = _g_paste_image_item_get_instance_private (_G_PASTE_IMAGE_ITEM (self));
    const GPasteImageItemPrivate *_priv = _g_paste_image_item_get_instance_private (_G_PASTE_IMAGE_ITEM (other));

    return g_paste_str_equal (priv->checksum, _priv->checksum);
}

static void
g_paste_image_item_set_size (GPasteItem *self)
{
    GPasteImageItemPrivate *priv = g_paste_image_item_get_instance_private (G_PASTE_IMAGE_ITEM (self));
    GdkPixbuf *image = priv->image;

    if (image)
    {
        if (!priv->additional_size)
        {
            priv->additional_size += strlen (priv->checksum) + 1 + gdk_pixbuf_get_byte_length (image);
            g_paste_item_add_size (self, priv->additional_size);
        }
    }
    else
    {
        g_paste_item_remove_size (self, priv->additional_size);
        priv->additional_size = 0;
    }
}

static const gchar *
g_paste_image_item_get_kind (const GPasteItem *self G_GNUC_UNUSED)
{
    return "Image";
}

static void
g_paste_image_item_set_state (GPasteItem     *self,
                              GPasteItemState state)
{
    GPasteImageItemPrivate *priv = g_paste_image_item_get_instance_private (G_PASTE_IMAGE_ITEM (self));

    switch (state)
    {
    case G_PASTE_ITEM_STATE_IDLE:
        if (priv->image)
        {
            g_clear_object (&priv->image);
            g_clear_pointer (&priv->checksum, g_free);
        }
        break;
    case G_PASTE_ITEM_STATE_ACTIVE:
        if (!priv->image)
        {
            priv->image = gdk_pixbuf_new_from_file (g_paste_item_get_value (self),
                                                    NULL); /* Error */
            priv->checksum = g_paste_gtk_util_compute_checksum (priv->image);
        }
        break;
    }

    g_paste_image_item_set_size (self);
}

static void
g_paste_image_item_dispose (GObject *object)
{
    GPasteImageItemPrivate *priv = g_paste_image_item_get_instance_private (G_PASTE_IMAGE_ITEM (object));
    GDateTime *date = priv->date;

    if (date)
    {
        g_date_time_unref (date);
        if (priv->image)
            g_object_unref (priv->image);
        priv->date = NULL;
    }

    G_OBJECT_CLASS (g_paste_image_item_parent_class)->dispose (object);
}

static void
g_paste_image_item_finalize (GObject *object)
{
    const GPasteImageItemPrivate *priv = _g_paste_image_item_get_instance_private (G_PASTE_IMAGE_ITEM (object));

    g_free (priv->checksum);

    G_OBJECT_CLASS (g_paste_image_item_parent_class)->finalize (object);
}

static void
g_paste_image_item_class_init (GPasteImageItemClass *klass)
{
    GPasteItemClass *item_class = G_PASTE_ITEM_CLASS (klass);

    item_class->equals = g_paste_image_item_equals;
    item_class->get_kind = g_paste_image_item_get_kind;
    item_class->set_state = g_paste_image_item_set_state;

    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->dispose = g_paste_image_item_dispose;
    gobject_class->finalize = g_paste_image_item_finalize;
}

static void
g_paste_image_item_init (GPasteImageItem *self G_GNUC_UNUSED)
{
}

static GPasteItem *
_g_paste_image_item_new (const gchar *path,
                         GDateTime   *date,
                         GdkPixbuf   *image,
                         gchar       *checksum)
{
    GPasteItem *self = g_paste_item_new (G_PASTE_TYPE_IMAGE_ITEM, path);
    GPasteImageItemPrivate *priv = g_paste_image_item_get_instance_private (G_PASTE_IMAGE_ITEM (self));

    priv->date = date;
    priv->image = image;

    if (image)
        priv->checksum = (checksum) ? checksum : g_paste_gtk_util_compute_checksum (image);
    else
        g_paste_image_item_set_state (G_PASTE_ITEM (self), G_PASTE_ITEM_STATE_ACTIVE);

    if (!priv->image || !GDK_IS_PIXBUF (priv->image))
    {
        g_object_unref (self);
        return NULL;
    }

    /* This is the date format "month/day/year time" */
    g_autofree gchar *formatted_date = g_date_time_format (date, _("%m/%d/%y %T"));
    /* This gets displayed in history when selecting an image */
    g_autofree gchar *display_string = g_strdup_printf (_("[Image, %d x %d (%s)]"),
                                                                  gdk_pixbuf_get_width (priv->image),
                                                                  gdk_pixbuf_get_height (priv->image),
                                                                  formatted_date);
    g_paste_item_set_display_string (self, display_string);

    if (image)
        g_paste_image_item_set_size (self);
    else
        g_paste_image_item_set_state (G_PASTE_ITEM (self), G_PASTE_ITEM_STATE_IDLE);

    return self;
}

/**
 * g_paste_image_item_new:
 * @img: (transfer none): the GdkPixbuf we want to be contained in the #GPasteImageItem
 *
 * Create a new instance of #GPasteImageItem
 *
 * Returns: a newly allocated #GPasteImageItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_image_item_new (GdkPixbuf *img)
{
    g_return_val_if_fail (GDK_IS_PIXBUF (img), NULL);

    gchar *checksum = g_paste_gtk_util_compute_checksum (img);
    g_autofree gchar *images_dir_path = g_build_filename (g_get_user_data_dir (), "gpaste", "images", NULL);
    g_autoptr (GFile) images_dir = g_file_new_for_path (images_dir_path);

    if (!g_file_query_exists (images_dir, NULL))
        mkdir (images_dir_path, (mode_t) 0700);

    g_autofree gchar *filename = g_strconcat (checksum, ".png", NULL);
    g_autofree gchar *path = g_build_filename (images_dir_path, filename, NULL);
    GPasteItem *self = _g_paste_image_item_new (path,
                                                g_date_time_new_now_local (),
                                                g_object_ref (img),
                                                checksum);

    gdk_pixbuf_save (img,
                     g_paste_item_get_value (self),
                     "png",
                     NULL, /* Error */
                     NULL); /* Params */

    return self;
}

/**
 * g_paste_image_item_new_from_file:
 * @path: the path to the image we want to be contained in the #GPasteImageItem
 * @date: (transfer none): the date at which the image was created
 *
 * Create a new instance of #GPasteImageItem
 *
 * Returns: a newly allocated #GPasteImageItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_image_item_new_from_file (const gchar *path,
                                  GDateTime   *date)
{
    g_return_val_if_fail (path, NULL);
    g_return_val_if_fail (g_utf8_validate (path, -1, NULL), NULL);
    g_return_val_if_fail (date, NULL);

    return _g_paste_image_item_new (path,
                                    g_date_time_ref (date),
                                    NULL, /* GdkPixbuf */
                                    NULL); /* Checksum */
}
