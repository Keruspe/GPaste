// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-util.h>
#include <gpaste-daemon/gpaste-image-item.h>

#include <gio/gio.h>
#include <string.h>

struct _GPasteImageItem
{
    GPasteItem parent_instance;

    gchar      *checksum;
    GDateTime  *date;
    GdkTexture *image;
    /* The encoded PNG, kept across IDLE (unlike the heavy decoded texture) so
     * the item never depends on its on-disk cache file: a storage backend can
     * persist it as a blob and hand it back on load, and the texture can be
     * rebuilt from it. NULL for items loaded by path only. */
    GBytes     *png;

    guint64    additional_size;
};

G_PASTE_DEFINE_TYPE (ImageItem, image_item, G_PASTE_TYPE_ITEM)

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

    return self->checksum;
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

    return self->date;
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

    return self->image;
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

    return self->png;
}

/* Attach the encoded PNG (transfer full) and account for the memory it keeps
 * across IDLE (unlike additional_size, which only tracks the decoded texture). */
static void
g_paste_image_item_take_png (GPasteItem *self,
                             GBytes     *png)
{
    GPasteImageItem *priv = G_PASTE_IMAGE_ITEM (self);

    priv->png = png;
    g_paste_item_add_size (self, g_bytes_get_size (png));
}

static gboolean
g_paste_image_item_equals (const GPasteItem *self,
                           const GPasteItem *other)
{
    if (!_G_PASTE_IS_IMAGE_ITEM (other))
        return FALSE;

    const GPasteImageItem *priv = _G_PASTE_IMAGE_ITEM (self);
    const GPasteImageItem *_priv = _G_PASTE_IMAGE_ITEM (other);

    return g_paste_str_equal (priv->checksum, _priv->checksum);
}

static void
g_paste_image_item_set_size (GPasteItem *self)
{
    GPasteImageItem *priv = G_PASTE_IMAGE_ITEM (self);
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
    GPasteImageItem *priv = G_PASTE_IMAGE_ITEM (self);

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
                priv->checksum = g_paste_image_item_compute_checksum (priv->image);
        }
        break;
    }

    g_paste_image_item_set_size (self);
}

static void
g_paste_image_item_dispose (GObject *object)
{
    GPasteImageItem *self = G_PASTE_IMAGE_ITEM (object);
    g_clear_pointer (&self->date, g_date_time_unref);
    g_clear_object (&self->image);
    g_clear_pointer (&self->png, g_bytes_unref);

    G_OBJECT_CLASS (g_paste_image_item_parent_class)->dispose (object);
}

static void
g_paste_image_item_finalize (GObject *object)
{
    const GPasteImageItem *self = G_PASTE_IMAGE_ITEM (object);

    g_free (self->checksum);

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
    GPasteImageItem *priv = G_PASTE_IMAGE_ITEM (self);

    priv->date = date;
    priv->image = image;
    priv->checksum = checksum; /* may be NULL, takes ownership */

    if (image)
    {
        if (!priv->checksum)
            priv->checksum = g_paste_image_item_compute_checksum (image);
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
 * g_paste_image_item_get_images_dir:
 * @history_name: the name of a history
 *
 * Get the directory @history_name's image files live in. This is the one owner
 * of the images/<history_name>/ layout: per history, so the same image copied
 * in several histories gets one file each and evicting it from one never
 * breaks the others.
 *
 * Returns: the images directory path
 */
G_PASTE_VISIBLE gchar *
g_paste_image_item_get_images_dir (const gchar *history_name)
{
    g_return_val_if_fail (history_name, NULL);

    g_autofree gchar *history_dir = g_paste_util_get_history_dir_path ();

    return g_build_filename (history_dir, "images", history_name, NULL);
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

/* The canonical on-disk cache location of an image:
 * <history-dir>/images/<history_name>/<checksum>.png. A NULL @history_name (an
 * item not yet added to a history) anchors directly under images/ as a
 * transient placeholder. */
static gchar *
g_paste_image_item_get_image_path (const gchar *history_name,
                                   const gchar *checksum)
{
    g_autofree gchar *filename = g_strconcat (checksum, ".png", NULL);

    if (history_name)
    {
        g_autofree gchar *images_dir = g_paste_image_item_get_images_dir (history_name);

        return g_build_filename (images_dir, filename, NULL);
    }

    g_autofree gchar *history_dir = g_paste_util_get_history_dir_path ();

    return g_build_filename (history_dir, "images", filename, NULL);
}

/**
 * g_paste_image_item_get_path_for_history:
 * @self: a #GPasteImageItem instance
 * @history_name: the name of a history
 *
 * Get the canonical cache path this image would live at in @history_name's own
 * images directory, e.g. for a storage backend writing the item under another
 * history's name (a backup) without touching the item itself.
 *
 * Returns: (nullable): the canonical cache path, or %NULL without a checksum
 */
G_PASTE_VISIBLE gchar *
g_paste_image_item_get_path_for_history (const GPasteImageItem *self,
                                         const gchar           *history_name)
{
    g_return_val_if_fail (_G_PASTE_IS_IMAGE_ITEM (self), NULL);
    g_return_val_if_fail (history_name, NULL);

    return (self->checksum) ? g_paste_image_item_get_image_path (history_name, self->checksum) : NULL;
}

/**
 * g_paste_image_item_set_history:
 * @self: a #GPasteImageItem instance
 * @history_name: the name of the history the item now belongs to
 *
 * Re-anchor the item's canonical cache path under @history_name's own images
 * directory. The history calls this when the item is added, before it is
 * persisted; items loaded from storage keep the path they were stored with
 * (that is where their materialized file actually lives).
 */
G_PASTE_VISIBLE void
g_paste_image_item_set_history (GPasteImageItem *self,
                                const gchar     *history_name)
{
    g_return_if_fail (_G_PASTE_IS_IMAGE_ITEM (self));
    g_return_if_fail (history_name);

    if (!self->checksum)
        return;

    g_autofree gchar *path = g_paste_image_item_get_image_path (history_name, self->checksum);

    g_paste_item_set_value (G_PASTE_ITEM (self), path);
}

/**
 * g_paste_image_item_compute_checksum:
 * @image: the #GdkTexture to checksum
 *
 * Compute the checksum of an image
 *
 * Returns: the newly allocated checksum
 */
G_PASTE_VISIBLE gchar *
g_paste_image_item_compute_checksum (GdkTexture *image)
{
    if (!image || !GDK_IS_TEXTURE (image))
        return NULL;

    gsize stride = (gsize) gdk_texture_get_width (image) * 4;
    gsize length = stride * gdk_texture_get_height (image);
    g_autofree guchar *data = g_malloc (length);

    gdk_texture_download (image, data, stride);

    return g_compute_checksum_for_data (G_CHECKSUM_SHA256, data, length);
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

    g_autofree gchar *checksum = g_paste_image_item_compute_checksum (texture);
    /* Transient anchor: the history re-anchors the item under its own images
     * directory when it is added. */
    g_autofree gchar *path = g_paste_image_item_get_image_path (NULL, checksum);
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

static GPasteItem *
_g_paste_image_item_new_from_bytes (const gchar *path,
                                    const gchar *history_name,
                                    GBytes      *png,
                                    GDateTime   *date,
                                    const gchar *checksum)
{
    g_autoptr (GError) error = NULL;
    GdkTexture *texture = gdk_texture_new_from_bytes (png, &error);

    if (!texture)
    {
        g_warning ("Failed to load image from its stored bytes: %s", error ? error->message : "unknown error");
        return NULL;
    }

    g_autofree gchar *sum = (checksum) ? g_strdup (checksum) : g_paste_image_item_compute_checksum (texture);
    g_autofree gchar *anchor = (path) ? g_strdup (path) : g_paste_image_item_get_image_path (history_name, sum);
    GPasteItem *self = _g_paste_image_item_new (anchor,
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
 * g_paste_image_item_new_from_bytes:
 * @history_name: the name of the history the item belongs to
 * @png: the encoded PNG we want to be contained in the #GPasteImageItem
 * @date: (transfer none): the date at which the image was created
 * @checksum: (nullable): the image's known SHA256 checksum, or %NULL to compute it
 *
 * Create a new instance of #GPasteImageItem from its encoded bytes (e.g. a
 * storage backend's blob), independent of any on-disk cache file. The item's
 * value is the canonical cache path the file would live at for @history_name,
 * but the file is neither read nor written.
 *
 * Returns: (nullable): a newly allocated #GPasteImageItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_image_item_new_from_bytes (const gchar *history_name,
                                   GBytes      *png,
                                   GDateTime   *date,
                                   const gchar *checksum)
{
    g_return_val_if_fail (history_name, NULL);
    g_return_val_if_fail (png, NULL);
    g_return_val_if_fail (date, NULL);

    return _g_paste_image_item_new_from_bytes (NULL, history_name, png, date, checksum);
}

/**
 * g_paste_image_item_new_from_bytes_at_path:
 * @path: the on-disk location the image was stored with
 * @png: the encoded PNG we want to be contained in the #GPasteImageItem
 * @date: (transfer none): the date at which the image was created
 * @checksum: (nullable): the image's known SHA256 checksum, or %NULL to compute it
 *
 * Like g_paste_image_item_new_from_bytes() but anchored at an explicit @path:
 * for storage backends whose reference *is* a path (e.g. the encrypted file
 * backend reading an image side file), so the item keeps pointing at where its
 * materialized data actually lives.
 *
 * Returns: (nullable): a newly allocated #GPasteImageItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_image_item_new_from_bytes_at_path (const gchar *path,
                                           GBytes      *png,
                                           GDateTime   *date,
                                           const gchar *checksum)
{
    g_return_val_if_fail (path, NULL);
    g_return_val_if_fail (png, NULL);
    g_return_val_if_fail (date, NULL);

    return _g_paste_image_item_new_from_bytes (path, NULL, png, date, checksum);
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
