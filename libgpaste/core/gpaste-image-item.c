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

#include "gpaste-image-item-private.h"

#include <glib/gi18n-lib.h>
#include <sys/stat.h>

#define G_PASTE_IMAGE_ITEM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_IMAGE_ITEM, GPasteImageItemPrivate))

G_DEFINE_TYPE (GPasteImageItem, g_paste_image_item, G_PASTE_TYPE_ITEM)

struct _GPasteImageItemPrivate
{
    gchar     *checksum;
    GDateTime *date;
    GdkPixbuf *image;
};

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
    g_return_val_if_fail (G_PASTE_IS_IMAGE_ITEM (self), NULL);

    return self->priv->checksum;
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
    g_return_val_if_fail (G_PASTE_IS_IMAGE_ITEM (self), NULL);

    return self->priv->date;
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
    g_return_val_if_fail (G_PASTE_IS_IMAGE_ITEM (self), NULL);

    return self->priv->image;
}

static gboolean
g_paste_image_item_equals (const GPasteItem *self,
                           const GPasteItem *other)
{
    g_return_val_if_fail (G_PASTE_IS_IMAGE_ITEM (self), FALSE);

    return (G_PASTE_IS_IMAGE_ITEM (other) &&
            (g_strcmp0 (G_PASTE_IMAGE_ITEM (self)->priv->checksum, G_PASTE_IMAGE_ITEM (other)->priv->checksum) == 0));
}

static gsize
g_paste_image_item_get_size (const GPasteItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_IMAGE_ITEM (self), FALSE);

    GPasteImageItemPrivate *priv = G_PASTE_IMAGE_ITEM (self)->priv;

    return G_PASTE_ITEM_CLASS (g_paste_image_item_parent_class)->get_size (self) +
        strlen (priv->checksum) + gdk_pixbuf_get_byte_length (priv->image);
}

static const gchar *
g_paste_image_item_get_kind (const GPasteItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_IMAGE_ITEM (self), NULL);

    return "Image";
}

static void
g_paste_image_item_set_state (GPasteItem     *self,
                              GPasteItemState state)
{
    g_return_if_fail (G_PASTE_IS_IMAGE_ITEM (self));

    GPasteImageItemPrivate *priv = G_PASTE_IMAGE_ITEM (self)->priv;

    switch (state)
    {
    case G_PASTE_ITEM_STATE_IDLE:
        if (priv->image)
            g_clear_object (&priv->image);
        break;
    case G_PASTE_ITEM_STATE_ACTIVE:
        if (!priv->image)
            priv->image = gdk_pixbuf_new_from_file (g_paste_item_get_value (G_PASTE_ITEM (self)),
                                                    NULL); /* Error */
        break;
    }
}

static void
g_paste_image_item_dispose (GObject *object)
{
    GPasteImageItemPrivate *priv = G_PASTE_IMAGE_ITEM (object)->priv;
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
    g_free (G_PASTE_IMAGE_ITEM (object)->priv->checksum);

    G_OBJECT_CLASS (g_paste_image_item_parent_class)->finalize (object);
}

static void
g_paste_image_item_class_init (GPasteImageItemClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPasteImageItemPrivate));

    GPasteItemClass *item_class = G_PASTE_ITEM_CLASS (klass);

    item_class->equals = g_paste_image_item_equals;
    item_class->get_size = g_paste_image_item_get_size;
    item_class->get_kind = g_paste_image_item_get_kind;
    item_class->set_state = g_paste_image_item_set_state;

    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->dispose = g_paste_image_item_dispose;
    gobject_class->finalize = g_paste_image_item_finalize;
}

static void
g_paste_image_item_init (GPasteImageItem *self)
{
    self->priv = G_PASTE_IMAGE_ITEM_GET_PRIVATE (self);
}

static GPasteImageItem *
_g_paste_image_item_new (const gchar *path,
                         GDateTime   *date,
                         GdkPixbuf   *image,
                         gchar       *checksum)
{
    GPasteItem *g_paste_item = g_paste_item_new (G_PASTE_TYPE_IMAGE_ITEM, path);
    GPasteImageItem *self = G_PASTE_IMAGE_ITEM (g_paste_item);
    GPasteImageItemPrivate *priv = self->priv;

    priv->date = date;
    priv->image = image;

    if (image)
    {
        if (!checksum)
        {
            guint length;
            const guchar *data = gdk_pixbuf_get_pixels_with_length (image,
                                                                    &length);
            checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA256,
                                                    data,
                                                    length);
        }
        priv->checksum = checksum;
        /* This is the date format "month/day/year time" */
        gchar *formatted_date = g_date_time_format (date, _("%m/%d/%y %T"));
        /* This gets displayed in history when selecting an image */
        gchar *display_string = g_strdup_printf (_("[Image, %d x %d (%s)]"),
                                                 gdk_pixbuf_get_width (image),
                                                 gdk_pixbuf_get_height (image),
                                                 formatted_date);
        g_paste_item_set_display_string (g_paste_item, display_string);
        g_free (display_string);
        g_free (formatted_date);
    }

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
G_PASTE_VISIBLE GPasteImageItem *
g_paste_image_item_new (GdkPixbuf *img)
{
    g_return_val_if_fail (GDK_IS_PIXBUF (img), NULL);

    guint length;
    const guchar *data = gdk_pixbuf_get_pixels_with_length (img,
                                                            &length);
    gchar *checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA256,
                                                   data,
                                                   length);
    gchar *images_dir_path = g_build_filename (g_get_user_data_dir (), "gpaste", "images", NULL);
    GFile *images_dir = g_file_new_for_path (images_dir_path);

    if (!g_file_query_exists (images_dir, NULL))
        mkdir (images_dir_path, (mode_t) 0700);
    g_object_unref (images_dir);

    gchar *filename = g_strconcat (checksum, ".png", NULL);
    gchar *path = g_build_filename (images_dir_path, filename, NULL);
    GPasteImageItem *self = _g_paste_image_item_new (path,
                                                     g_date_time_new_now_local (),
                                                     g_object_ref (img),
                                                     checksum);
    g_free (images_dir_path);
    g_free (filename);
    g_free (path);

    gdk_pixbuf_save (img,
                     g_paste_item_get_value (G_PASTE_ITEM (self)),
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
G_PASTE_VISIBLE GPasteImageItem *
g_paste_image_item_new_from_file (const gchar *path,
                                  GDateTime   *date)
{
    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (g_utf8_validate (path, -1, NULL), NULL);
    g_return_val_if_fail (date != NULL, NULL);

    return _g_paste_image_item_new (path,
                                    g_date_time_ref (date),
                                    NULL, /* GdkPixbuf */
                                    NULL); /* Checksum */
}
