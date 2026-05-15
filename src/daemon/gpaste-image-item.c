/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste/gpaste-util.h>
#include <gpaste-image-item.h>

#include <gio/gio.h>
#include <string.h>
#include <sys/stat.h>

struct _GPasteImageItem
{
    GPasteItem parent_instance;
};

typedef struct _GPasteImageItemPrivate
{
    gchar      *checksum;
    GDateTime  *date;
    GdkTexture *image;

    guint64    additional_size;
} GPasteImageItemPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (ImageItem, image_item, G_PASTE_TYPE_ITEM)

/**
 * g_paste_image_item_get_checksum:
 * @self: a #GPasteImageItem instance
 *
 * Get the checksum of the GdkTexture contained in the #GPasteImageItem
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
 * Returns: (transfer none): the GdkTexture of the image
 */
G_PASTE_VISIBLE GdkTexture *
g_paste_image_item_get_image (const GPasteImageItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_IMAGE_ITEM (self), NULL);

    const GPasteImageItemPrivate *priv = _g_paste_image_item_get_instance_private (self);

    return priv->image;
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
    GdkTexture *image = priv->image;

    if (image)
    {
        if (!priv->additional_size)
        {
            priv->additional_size += strlen (priv->checksum) + 1 + (gsize) gdk_texture_get_width (image) * gdk_texture_get_height (image) * 4;
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
            g_autoptr (GError) error = NULL;
            priv->image = gdk_texture_new_from_filename (g_paste_item_get_value (self), &error);
            if (error)
                g_warning ("Failed to load image from %s: %s", g_paste_item_get_value (self), error->message);
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
    g_clear_pointer (&priv->date, g_date_time_unref);
    g_clear_object (&priv->image);

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
                         GdkTexture  *image,
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

    if (!priv->image || !GDK_IS_TEXTURE (priv->image))
    {
        g_object_unref (self);
        return NULL;
    }

    /* This is the date format "month/day/year time" */
    g_autofree gchar *formatted_date = g_date_time_format (date, _("%m/%d/%y %T"));
    /* This gets displayed in history when selecting an image */
    g_autofree gchar *display_string = g_strdup_printf (_("[Image, %d x %d (%s)]"),
                                                                  gdk_texture_get_width (priv->image),
                                                                  gdk_texture_get_height (priv->image),
                                                                  formatted_date);
    g_paste_item_set_display_string (self, display_string);

    if (image)
        g_paste_image_item_set_size (self);
    else
        g_paste_image_item_set_state (G_PASTE_ITEM (self), G_PASTE_ITEM_STATE_IDLE);

    return self;
}

typedef struct
{
    GdkTexture *texture;
    gchar      *path;
} GPasteImageSaveData;

static void
g_paste_image_save_data_free (gpointer data)
{
    g_autofree GPasteImageSaveData *d = data;
    g_clear_object (&d->texture);
    g_clear_pointer (&d->path, g_free);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GPasteImageSaveData, g_paste_image_save_data_free)

static void g_paste_image_save_done (GObject *source_object, GAsyncResult *result, gpointer user_data);

static void
g_paste_image_save_task (GTask        *task,
                         gpointer      source_object G_GNUC_UNUSED,
                         gpointer      task_data,
                         GCancellable *cancellable G_GNUC_UNUSED)
{
    const GPasteImageSaveData *data = task_data;
    g_task_return_boolean (task, gdk_texture_save_to_png (data->texture, data->path));
}

static void
g_paste_image_save_done (GObject      *source_object G_GNUC_UNUSED,
                         GAsyncResult *result,
                         gpointer      user_data G_GNUC_UNUSED)
{
    g_autoptr (GError) error = NULL;
    if (!g_task_propagate_boolean (G_TASK (result), &error))
    {
        const GPasteImageSaveData *data = g_task_get_task_data (G_TASK (result));
        g_warning ("Failed to save image to %s: %s", data->path, error ? error->message : "unknown error");
    }
}

/**
 * g_paste_image_item_new:
 * @texture: (transfer none): the GdkTexture we want to be contained in the #GPasteImageItem
 *
 * Create a new instance of #GPasteImageItem
 *
 * Returns: a newly allocated #GPasteImageItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_image_item_new (GdkTexture *texture)
{
    g_return_val_if_fail (GDK_IS_TEXTURE (texture), NULL);

    gchar *checksum = g_paste_gtk_util_compute_checksum (texture);
    g_autofree gchar *history_dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *images_dir_path = g_build_filename (history_dir, "images", NULL);
    g_autoptr (GFile) images_dir = g_file_new_for_path (images_dir_path);

    if (!g_file_query_exists (images_dir, NULL))
        mkdir (images_dir_path, (mode_t) 0700);

    g_autofree gchar *filename = g_strconcat (checksum, ".png", NULL);
    g_autofree gchar *path = g_build_filename (images_dir_path, filename, NULL);
    GPasteItem *self = _g_paste_image_item_new (path,
                                                g_date_time_new_now_local (),
                                                g_object_ref (texture),
                                                checksum);

    g_autoptr (GPasteImageSaveData) data = g_new (GPasteImageSaveData, 1);
    data->texture = g_object_ref (texture);
    data->path = g_strdup (g_paste_item_get_value (self));

    g_autoptr (GTask) task = g_task_new (NULL, NULL, g_paste_image_save_done, NULL);
    g_task_set_static_name (task, "gpaste-image-save");
    g_task_set_task_data (task, g_steal_pointer (&data), g_paste_image_save_data_free);
    g_task_run_in_thread (task, g_paste_image_save_task);

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
                                    NULL, /* GdkTexture */
                                    NULL); /* Checksum */
}
