/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "config.h"
#include "gpaste-item-private.h"

#include <glib/gi18n-lib.h>
#include <sys/stat.h>
#include <sys/types.h>

/* GPaste Item */

#define G_PASTE_ITEM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_ITEM, GPasteItemPrivate))

G_DEFINE_ABSTRACT_TYPE (GPasteItem, g_paste_item, G_TYPE_OBJECT)

struct _GPasteItemPrivate
{
    gchar *value;
    gchar *display_string;
};

/**
 * g_paste_item_get_value:
 * @self: a GPasteItem instance
 *
 * Get the value of the given item (text, uris or path to the image)
 *
 * Returns: read-only string containing the value
 */
const gchar *
g_paste_item_get_value (const GPasteItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_ITEM (self), NULL);

    return self->priv->value;
}

/**
 * g_paste_item_get_display_string:
 * @self: a GPasteItem instance
 *
 * Get the string we should use to display the GPasteItem
 *
 * Returns: read-only display string
 */
const gchar *
g_paste_item_get_display_string (const GPasteItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_ITEM (self), NULL);

    return self->priv->display_string;
}

/**
 * g_paste_item_equals:
 * @self: a GPasteItem instance
 * @other: another GPasteItem instance
 *
 * Compare the two instances
 *
 * Returns: true if equals, false otherwise
 */
gboolean
g_paste_item_equals (const GPasteItem *self,
                     const GPasteItem *other)
{
    return G_PASTE_ITEM_GET_CLASS (self)->equals (self, other);
}

/**
 * g_paste_item_has_value:
 * @self: a GPasteItem instance
 *
 * Tell if the GPasteItem has a value or not
 *
 * Returns: true if it has value, false if it's a dummy one
 */
gboolean 
g_paste_item_has_value (const GPasteItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_ITEM (self), FALSE);

    return G_PASTE_ITEM_GET_CLASS (self)->has_value (self);
}

/**
 * g_paste_item_get_kind:
 * @self: a GPasteItem instance
 *
 * Get the kind of GPasteItem as string (for serialization)
 *
 * Returns: read-only string containing the kind of GPasteItem
 *          can be "Text", "Uris" or "Image"
 */
const gchar *
g_paste_item_get_kind (const GPasteItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_ITEM (self), NULL);

    return G_PASTE_ITEM_GET_CLASS (self)->get_kind (self);
}

static void
g_paste_item_finalize (GObject *object)
{
    g_free (G_PASTE_ITEM (object)->priv->value);
    
    G_OBJECT_CLASS (g_paste_item_parent_class)->finalize (object);
}

static gboolean
g_paste_item_default_equals (const GPasteItem *self,
                             const GPasteItem *other)
{
    g_return_val_if_fail (G_PASTE_IS_ITEM (self), FALSE);
    g_return_val_if_fail (G_PASTE_IS_ITEM (other), FALSE);

    return (g_strcmp0 (self->priv->value, other->priv->value) == 0);
}

static gboolean
g_paste_item_default_has_value (const GPasteItem *self)
{
    return FALSE;
}

static void
g_paste_item_class_init (GPasteItemClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPasteItemPrivate));
    
    klass->equals = g_paste_item_default_equals;
    klass->has_value = g_paste_item_default_has_value;
    klass->get_kind = NULL;
    
    G_OBJECT_CLASS (klass)->finalize = g_paste_item_finalize;
}

static void
g_paste_item_init (GPasteItem *self)
{
    self->priv = G_PASTE_ITEM_GET_PRIVATE (self);
}

static GPasteItem *
g_paste_item_new (GType        type,
                  const gchar *value)
{
    GPasteItem *self = g_object_new (type, NULL);
    
    self->priv->value = g_strdup (value);
    
    return self;
}

/* GPaste TextItem */

G_DEFINE_TYPE (GPasteTextItem, g_paste_text_item, G_PASTE_TYPE_ITEM)

static gboolean
g_paste_text_item_equals (const GPasteItem *self,
                          const GPasteItem *other)
{
    g_return_val_if_fail (G_PASTE_IS_TEXT_ITEM (self), FALSE);

    return (G_PASTE_IS_TEXT_ITEM (other) &&
            G_PASTE_ITEM_CLASS (g_paste_text_item_parent_class)->equals (self, other));
}

static const gchar *
g_paste_text_item_get_kind (const GPasteItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_TEXT_ITEM (self), NULL);

    return "Text";
}

static gboolean
g_paste_text_item_has_value (const GPasteItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_TEXT_ITEM (self), FALSE);

    return (g_strcmp0 ("", g_strstrip (self->priv->value)) != 0);
}

static void
g_paste_text_item_class_init (GPasteTextItemClass *klass)
{
    GPasteItemClass *item_class = G_PASTE_ITEM_CLASS (klass);
    
    item_class->equals = g_paste_text_item_equals;
    item_class->has_value = g_paste_text_item_has_value;
    item_class->get_kind = g_paste_text_item_get_kind;
}

static void
g_paste_text_item_init (GPasteTextItem *self)
{
    /* Silence warning */
    self = self;
}

static GPasteItem *
_g_paste_text_item_new (GType        type,
                        const gchar *text)
{
    GPasteItem *self = g_paste_item_new (type, text);
    
    self->priv->display_string = self->priv->value;
    
    return self;
}

/**
 * g_paste_text_item_new:
 * @text: the content of the desired GPasteTextItem
 *
 * Create a new instance of GPasteTextItem
 *
 * Returns: a newly allocated GPasteTextItem
 *          free it with g_object_unref
 */
GPasteTextItem *
g_paste_text_item_new (const gchar *text)
{
    return G_PASTE_TEXT_ITEM (_g_paste_text_item_new (G_PASTE_TYPE_TEXT_ITEM, text));
}

/* GPaste UrisItem */

#define G_PASTE_URIS_ITEM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_URIS_ITEM, GPasteUrisItemPrivate))

G_DEFINE_TYPE (GPasteUrisItem, g_paste_uris_item, G_PASTE_TYPE_TEXT_ITEM)

struct _GPasteUrisItemPrivate
{
    gchar **uris;
};

/**
 * g_paste_uris_item_get_uris:
 * @self: a GPasteUrisItem instance
 *
 * Get the list of uris contained in the GPasteUrisItem
 *
 * Returns: (transfer none): read-only array of read-only uris (strings)
 */
const gchar const **
g_paste_uris_item_get_uris (const GPasteUrisItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_URIS_ITEM (self), FALSE);

    return (const gchar const **) self->priv->uris;
}

static gboolean
g_paste_uris_item_equals (const GPasteItem *self,
                          const GPasteItem *other)
{
    g_return_val_if_fail (G_PASTE_IS_URIS_ITEM (self), FALSE);

    return (G_PASTE_IS_URIS_ITEM (other) &&
            G_PASTE_ITEM_CLASS (g_paste_uris_item_parent_class)->equals (self, other));
}

static const gchar *
g_paste_uris_item_get_kind (const GPasteItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_URIS_ITEM (self), NULL);

    return "Uris";
}

static gboolean
g_paste_uris_item_has_value (const GPasteItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_URIS_ITEM (self), FALSE);

    return G_PASTE_ITEM_CLASS (g_paste_uris_item_parent_class)->has_value (self);
}

static void
g_paste_uris_item_finalize (GObject *object)
{
    g_free (G_PASTE_ITEM (object)->priv->display_string);
    g_strfreev (G_PASTE_URIS_ITEM (object)->priv->uris);

    G_OBJECT_CLASS (g_paste_uris_item_parent_class)->finalize (object);
}

static void
g_paste_uris_item_class_init (GPasteUrisItemClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPasteUrisItemPrivate));
    
    GPasteItemClass *item_class = G_PASTE_ITEM_CLASS (klass);
    
    item_class->equals = g_paste_uris_item_equals;
    item_class->has_value = g_paste_uris_item_has_value;
    item_class->get_kind = g_paste_uris_item_get_kind;

    G_OBJECT_CLASS (klass)->finalize = g_paste_uris_item_finalize;
}

static void
g_paste_uris_item_init (GPasteUrisItem *self)
{
    self->priv = G_PASTE_URIS_ITEM_GET_PRIVATE (self);
}

/**
 * g_paste_uris_item_new:
 * @uris: a string containing the paths separated by "\n" (as returned by gtk_clipboard_wait_for_uris)
 *
 * Create a new instance of GPasteUrisItem
 *
 * Returns: a newly allocated GPasteUrisItem
 *          free it with g_object_unref
 */
GPasteUrisItem *
g_paste_uris_item_new (const gchar *uris)
{
    GPasteItem *g_paste_item = _g_paste_text_item_new (G_PASTE_TYPE_URIS_ITEM, uris);
    GPasteUrisItem *self = G_PASTE_URIS_ITEM (g_paste_item);
    GPasteUrisItemPrivate *priv = self->priv;

    gchar *home_escaped = g_regex_escape_string (g_get_home_dir (), -1);
    GRegex *regex = g_regex_new (home_escaped,
                                 0, /* Compile options */
                                 0, /* Match options */
                                 NULL); /* Error */
    gchar *display_string_with_newlines = g_regex_replace_literal (regex,
                                                                   uris,
                                                                   (gssize) -1,
                                                                   0, /* Start position */
                                                                   "~",
                                                                   0, /* Match options */
                                                                   NULL); /* Error */
    g_regex_unref (regex);
    g_free (home_escaped);

    regex = g_regex_new ("\\n",
                         0, /* Compile options */
                         0, /* Match options */
                         NULL); /* Error */
    gchar *display_string = g_regex_replace_literal (regex,
                                                     display_string_with_newlines,
                                                     (gssize) -1,
                                                     0, /* Start position */
                                                     " ",
                                                     0, /* Match options */
                                                     NULL); /* Error */
    g_regex_unref (regex);
    g_free (display_string_with_newlines);

    // This is the prefix displayed in history to identify selected files
    g_paste_item->priv->display_string = g_strconcat (_("[Files] "), display_string, NULL);
    g_free (display_string);

    gchar **paths = g_strsplit (uris, "\n", 0);
    guint length = g_strv_length (paths);

    priv->uris = g_new (gchar *, length + 1);
    for (guint i = 0; i < length; ++i)
        priv->uris[i] = g_strconcat ("file://", paths[i], NULL);
    priv->uris[length] = NULL;
    g_strfreev (paths);

    return self;
}

/* GPaste ImageItem */

#define G_PASTE_IMAGE_ITEM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_IMAGE_ITEM, GPasteImageItemPrivate))

G_DEFINE_TYPE (GPasteImageItem, g_paste_image_item, G_PASTE_TYPE_ITEM)

struct _GPasteImageItemPrivate
{
    gchar *checksum;
    GDateTime *date;
    GdkPixbuf *image;
};

/**
 * g_paste_image_item_get_checksum:
 * @self: a GPasteImageItem instance
 *
 * Get the checksum of the GdkPixbuf contained in the GPasteImageItem
 *
 * Returns: read-only string representatig the SHA256 checksum of the image
 */
const gchar *
g_paste_image_item_get_checksum (const GPasteImageItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_IMAGE_ITEM (self), NULL);

    return self->priv->checksum;
}

/**
 * g_paste_image_item_get_date:
 * @self: a GPasteImageItem instance
 *
 * Get the date at which the image was created
 *
 * Returns: read-only GDateTime containing the image's creation date
 */
const GDateTime *
g_paste_image_item_get_date (const GPasteImageItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_IMAGE_ITEM (self), NULL);

    return self->priv->date;
}

/**
 * g_paste_image_item_get_image:
 * @self: a GPasteImageItem instance
 *
 * Get the image contained in the GPasteImageItem
 *
 * Returns: read-only GdkPixbuf of the image
 */
const GdkPixbuf *
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

static const gchar *
g_paste_image_item_get_kind (const GPasteItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_IMAGE_ITEM (self), NULL);

    return "Image";
}

static gboolean
g_paste_image_item_has_value (const GPasteItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_IMAGE_ITEM (self), FALSE);

    return TRUE;
}

static void
g_paste_image_item_dispose (GObject *object)
{
    GPasteImageItemPrivate *priv = G_PASTE_IMAGE_ITEM (object)->priv;

    g_date_time_unref (priv->date);
    gdk_pixbuf_unref (priv->image);

    G_OBJECT_CLASS (g_paste_image_item_parent_class)->dispose (object);
}

static void
g_paste_image_item_finalize (GObject *object)
{
    g_free (G_PASTE_ITEM (object)->priv->display_string);
    g_free (G_PASTE_IMAGE_ITEM (object)->priv->checksum);

    G_OBJECT_CLASS (g_paste_image_item_parent_class)->finalize (object);
}

static void
g_paste_image_item_class_init (GPasteImageItemClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPasteImageItemPrivate));
    
    GPasteItemClass *item_class = G_PASTE_ITEM_CLASS (klass);
    
    item_class->equals = g_paste_image_item_equals;
    item_class->has_value = g_paste_image_item_has_value;
    item_class->get_kind = g_paste_image_item_get_kind;

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
                         GdkPixbuf   *image,
                         GDateTime   *date,
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
            checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA256,
                                                    (guchar *) gdk_pixbuf_get_pixels (image),
                                                    -1);
        priv->checksum = checksum;
        /* This is the date format "month/day/year time" */
        gchar *formatted_date = g_date_time_format (date, _("%m/%d/%y %T"));
        /* This gets displayed in history when selecting an image */
        g_paste_item->priv->display_string = g_strdup_printf (_("[Image, %d x %d (%s)]"),
                                                             gdk_pixbuf_get_width (image),
                                                             gdk_pixbuf_get_height (image),
                                                             formatted_date);
        g_free (formatted_date);
    }

    return self;
}

/**
 * g_paste_image_item_new:
 * @img: (transfer none): the GdkPixbuf we want to be contained in the GPasteImageItem
 *
 * Create a new instance of GPasteImageItem
 *
 * Returns: a newly allocated GPasteImageItem
 *          free it with g_object_unref
 */
GPasteImageItem *
g_paste_image_item_new (GdkPixbuf *img)
{
    gchar *checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA256,
                                                   (guchar *) gdk_pixbuf_get_pixels (img),
                                                   -1);
    gchar *images_dir_path = g_build_filename (g_get_user_data_dir (), "gpaste", "images", NULL);
    GFile *images_dir = g_file_new_for_path (images_dir_path);

    if (!g_file_query_exists (images_dir, NULL))
        mkdir (images_dir_path, (mode_t) 0700);
    g_object_unref (images_dir);

    gchar *filename = g_strconcat (checksum, ".png", NULL);
    gchar *path = g_build_filename (images_dir_path, filename, NULL);
    GPasteImageItem *self = _g_paste_image_item_new (path,
                                                     gdk_pixbuf_ref (img),
                                                     g_date_time_new_now_local (),
                                                     checksum);
    g_free (images_dir_path);
    g_free (filename);
    g_free (path);

    gdk_pixbuf_save (img,
                     G_PASTE_ITEM (self)->priv->value,
                     "png",
                     NULL, /* Error */
                     NULL); /* Params */

    return self;
}

/**
 * g_paste_image_item_new_from_file:
 * @path: the path to the image we want to be contained in the GPasteImageItem
 * @date: (transfer none): the date at which the image was created
 *
 * Create a new instance of GPasteImageItem
 *
 * Returns: a newly allocated GPasteImageItem
 *          free it with g_object_unref
 */
GPasteImageItem *
g_paste_image_item_new_from_file (const gchar *path,
                                  GDateTime   *date)
{
    GdkPixbuf *image = gdk_pixbuf_new_from_file (path,
                                                 NULL); /* Error */

    return _g_paste_image_item_new (path,
                                    image,
                                    g_date_time_ref (date),
                                    NULL);
}
