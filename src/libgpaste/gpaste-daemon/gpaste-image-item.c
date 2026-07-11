// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste/gpaste-util.h>
#include <gpaste-daemon/gpaste-image-item.h>

#include <gio/gio.h>
#include <string.h>

struct _GPasteImageItem
{
    GPasteItem parent_instance;
};

typedef struct _GPasteImageItemPrivate
{
    gchar      *checksum;
    GDateTime  *date;
    GdkTexture *image;
    /* The encoded PNG, kept across IDLE (unlike the heavy decoded texture) so
     * the item never depends on its on-disk cache file: a storage backend can
     * persist it as a blob and hand it back on load, and the texture can be
     * rebuilt from it. NULL for items loaded by path only. */
    GBytes     *png;

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

/**
 * g_paste_image_item_get_png_bytes:
 * @self: a #GPasteImageItem instance
 *
 * Get the encoded PNG contained in the #GPasteImageItem, when it carries one
 * (an item loaded from its on-disk cache file by path does not)
 *
 * Returns: (transfer none) (nullable): the PNG bytes
 */
G_PASTE_VISIBLE GBytes *
g_paste_image_item_get_png_bytes (const GPasteImageItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_IMAGE_ITEM (self), NULL);

    const GPasteImageItemPrivate *priv = _g_paste_image_item_get_instance_private (self);

    return priv->png;
}

/* Attach the encoded PNG (transfer full) and account for the memory it keeps
 * across IDLE (unlike additional_size, which only tracks the decoded texture). */
static void
g_paste_image_item_take_png (GPasteItem *self,
                             GBytes     *png)
{
    GPasteImageItemPrivate *priv = g_paste_image_item_get_instance_private (G_PASTE_IMAGE_ITEM (self));

    priv->png = png;
    g_paste_item_add_size (self, g_bytes_get_size (png));
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
        /* Drop only the heavy texture; keep the checksum so deduplication
         * keeps working against idle items already in the history. */
        g_clear_object (&priv->image);
        break;
    case G_PASTE_ITEM_STATE_ACTIVE:
        if (!priv->image)
        {
            g_autoptr (GError) error = NULL;
            priv->image = (priv->png)
                ? gdk_texture_new_from_bytes (priv->png, &error)
                : gdk_texture_new_from_filename (g_paste_item_get_value (self), &error);
            if (error)
                g_warning ("Failed to load image from %s: %s", g_paste_item_get_value (self), error->message);
            if (!priv->checksum)
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
    g_clear_pointer (&priv->png, g_bytes_unref);

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
    priv->checksum = checksum; /* may be NULL, takes ownership */

    if (image)
    {
        if (!priv->checksum)
            priv->checksum = g_paste_gtk_util_compute_checksum (image);
    }
    else
        g_paste_image_item_set_state (G_PASTE_ITEM (self), G_PASTE_ITEM_STATE_ACTIVE);

    if (!priv->image || !GDK_IS_TEXTURE (priv->image))
    {
        g_object_unref (self);
        return NULL;
    }

    /* Translators: strftime format for image timestamps. Rearrange to match your locale's date/time convention. */
    g_autofree gchar *formatted_date = g_date_time_format (date, _("%m/%d/%y %T"));
    /* Translators: Image item displayed in history. %d is width, %d is height, %s is the formatted date. */
    g_autofree gchar *display_string = g_strdup_printf (_("[Image, %d x %d (%s)]"),
                                                                  gdk_texture_get_width (priv->image),
                                                                  gdk_texture_get_height (priv->image),
                                                                  formatted_date);
    g_paste_item_set_display_string (self, g_steal_pointer (&display_string));

    if (image)
        g_paste_image_item_set_size (self);
    else
        g_paste_image_item_set_state (G_PASTE_ITEM (self), G_PASTE_ITEM_STATE_IDLE);

    return self;
}

/**
 * g_paste_image_item_get_encrypted_path:
 * @path: the canonical (plain) cache path of an image
 *
 * Get the path of the encrypted side file the encrypted file backend
 * materializes for @path (".pngs", mirroring ".xml"/".xmls"). This is the one
 * owner of that naming scheme.
 *
 * Returns: the encrypted side file path
 */
G_PASTE_VISIBLE gchar *
g_paste_image_item_get_encrypted_path (const gchar *path)
{
    g_return_val_if_fail (path, NULL);

    return g_strconcat (path, "s", NULL);
}

/**
 * g_paste_image_item_delete_files:
 * @path: the canonical (plain) cache path of an image
 *
 * Delete an image's materialized data: the cache file at @path and its
 * encrypted side file. The storage backend owns which of them was actually
 * written (a database blob writes neither), so whichever is absent is fine.
 */
G_PASTE_VISIBLE void
g_paste_image_item_delete_files (const gchar *path)
{
    g_return_if_fail (path);

    g_autofree gchar *encrypted_path = g_paste_image_item_get_encrypted_path (path);
    const gchar *paths[] = { path, encrypted_path };

    for (guint64 i = 0; i < G_N_ELEMENTS (paths); ++i)
    {
        g_autoptr (GFile) image = g_file_new_for_path (paths[i]);
        g_autoptr (GError) error = NULL;

        if (!g_file_delete (image, NULL, &error) &&
            !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            g_warning ("Failed to delete image file: %s", error->message);
    }
}

/* The canonical on-disk cache location of an image: <history-dir>/images/<checksum>.png */
static gchar *
g_paste_image_item_get_image_path (const gchar *checksum)
{
    g_autofree gchar *history_dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *filename = g_strconcat (checksum, ".png", NULL);

    return g_build_filename (history_dir, "images", filename, NULL);
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

    g_autofree gchar *checksum = g_paste_gtk_util_compute_checksum (texture);
    g_autofree gchar *path = g_paste_image_item_get_image_path (checksum);
    GPasteItem *self = _g_paste_image_item_new (path,
                                                g_date_time_new_now_local (),
                                                g_object_ref (texture),
                                                g_strdup (checksum));
    if (!self)
        return NULL;

    /* Encode once and carry the PNG: persisting the image (as a database blob,
     * a plain cache file, an encrypted side file...) is the storage backend's
     * business, and D-Bus clients get the bytes without touching the disk. */
    g_paste_image_item_take_png (self, gdk_texture_save_to_png_bytes (texture));

    return self;
}

/**
 * g_paste_image_item_new_from_bytes:
 * @png: the encoded PNG we want to be contained in the #GPasteImageItem
 * @date: (transfer none): the date at which the image was created
 * @checksum: (nullable): the image's known SHA256 checksum, or %NULL to compute it
 *
 * Create a new instance of #GPasteImageItem from its encoded bytes (e.g. a
 * storage backend's blob), independent of any on-disk cache file. The item's
 * value is the canonical cache path the file would live at, but the file is
 * neither read nor written.
 *
 * Returns: (nullable): a newly allocated #GPasteImageItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_image_item_new_from_bytes (GBytes      *png,
                                   GDateTime   *date,
                                   const gchar *checksum)
{
    g_return_val_if_fail (png, NULL);
    g_return_val_if_fail (date, NULL);

    g_autoptr (GError) error = NULL;
    GdkTexture *texture = gdk_texture_new_from_bytes (png, &error);

    if (!texture)
    {
        g_warning ("Failed to load image from its stored bytes: %s", error ? error->message : "unknown error");
        return NULL;
    }

    g_autofree gchar *sum = (checksum) ? g_strdup (checksum) : g_paste_gtk_util_compute_checksum (texture);
    g_autofree gchar *path = g_paste_image_item_get_image_path (sum);
    GPasteItem *self = _g_paste_image_item_new (path,
                                                g_date_time_ref (date),
                                                texture,
                                                g_steal_pointer (&sum));

    if (!self)
        return NULL;

    g_paste_image_item_take_png (self, g_bytes_ref (png));
    /* Like an item loaded by path: rest in the history without the decoded
     * texture (the PNG bytes stay to rebuild it on activation). */
    g_paste_item_set_state (self, G_PASTE_ITEM_STATE_IDLE);

    return self;
}

/**
 * g_paste_image_item_new_from_file:
 * @path: the path to the image we want to be contained in the #GPasteImageItem
 * @date: (transfer none): the date at which the image was created
 * @checksum: (nullable): the image's known SHA256 checksum, or %NULL to compute it
 *
 * Create a new instance of #GPasteImageItem
 *
 * Returns: a newly allocated #GPasteImageItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_image_item_new_from_file (const gchar *path,
                                  GDateTime   *date,
                                  const gchar *checksum)
{
    g_return_val_if_fail (path, NULL);
    g_return_val_if_fail (g_utf8_validate (path, -1, NULL), NULL);
    g_return_val_if_fail (date, NULL);

    return _g_paste_image_item_new (path,
                                    g_date_time_ref (date),
                                    NULL, /* GdkTexture */
                                    g_strdup (checksum)); /* Checksum (may be NULL) */
}
