// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-image-item.h>

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
    /* Where this image's bytes were materialized, when they were anywhere at
     * all: set by the storage backend that read the item back off a file. NULL
     * for a fresh capture and for a backend keeping its images inside its own
     * store, so everything reaching for a file has the one thing to check. */
    gchar      *cache_path;

    guint64     additional_size;
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
g_paste_image_item_get_checksum (GPasteImageItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_IMAGE_ITEM (self), NULL);

    return self->checksum;
}

/**
 * g_paste_image_item_get_cache_path:
 * @self: a #GPasteImageItem instance
 *
 * Get the file this image's bytes were materialized in, if any. Only a backend
 * that stores images as files gives an item one; a database blob is read back
 * as bytes and never touches the disk, and a freshly captured image has not
 * been persisted at all yet.
 *
 * Returns: (nullable): read-only path, or %NULL when no file holds this image
 */
G_PASTE_VISIBLE const gchar *
g_paste_image_item_get_cache_path (GPasteImageItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_IMAGE_ITEM (self), NULL);

    return self->cache_path;
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
g_paste_image_item_get_date (GPasteImageItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_IMAGE_ITEM (self), NULL);

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
g_paste_image_item_get_image (GPasteImageItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_IMAGE_ITEM (self), NULL);

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
g_paste_image_item_get_png_bytes (GPasteImageItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_IMAGE_ITEM (self), NULL);

    return self->png;
}

/* Attach the encoded PNG (transfer full) and account for the memory it keeps
 * across IDLE (unlike additional_size, which only tracks the decoded texture). */
static void
g_paste_image_item_take_png (GPasteItem *item,
                             GBytes     *png)
{
    GPasteImageItem *self = G_PASTE_IMAGE_ITEM (item);

    self->png = png;
    g_paste_item_add_size (item, g_bytes_get_size (png));
}

static gboolean
g_paste_image_item_equals (GPasteItem *item,
                           GPasteItem *other)
{
    if (!G_PASTE_IS_IMAGE_ITEM (other))
        return FALSE;

    GPasteImageItem *self = G_PASTE_IMAGE_ITEM (item);
    GPasteImageItem *_other = G_PASTE_IMAGE_ITEM (other);

    return g_paste_str_equal (self->checksum, _other->checksum);
}

static void
g_paste_image_item_set_size (GPasteItem *item)
{
    GPasteImageItem *self = G_PASTE_IMAGE_ITEM (item);
    GdkTexture *image = self->image;

    if (image)
    {
        if (!self->additional_size)
        {
            self->additional_size += strlen (self->checksum) + 1 + (gsize) gdk_texture_get_width (image) * gdk_texture_get_height (image) * 4;
            g_paste_item_add_size (item, self->additional_size);
        }
    }
    else
    {
        g_paste_item_remove_size (item, self->additional_size);
        self->additional_size = 0;
    }
}

static GPasteItemKind
g_paste_image_item_get_kind (GPasteItem *self G_GNUC_UNUSED)
{
    return G_PASTE_ITEM_KIND_IMAGE;
}

static void
g_paste_image_item_set_state (GPasteItem     *item,
                              GPasteItemState state)
{
    GPasteImageItem *self = G_PASTE_IMAGE_ITEM (item);

    switch (state)
    {
    case G_PASTE_ITEM_STATE_IDLE:
        /* Drop only the heavy texture; keep the checksum so deduplication
         * keeps working against idle items already in the history. */
        g_clear_object (&self->image);
        break;
    case G_PASTE_ITEM_STATE_ACTIVE:
        /* Rebuilt from whichever of the two the item has: the bytes it carries,
         * or the file a backend materialized for it. With neither there is no
         * image left to show and nothing to try. */
        if (!self->image && (self->png || self->cache_path))
        {
            g_autoptr (GError) error = NULL;
            const gchar *source = (self->png) ? "its stored bytes" : self->cache_path;

            self->image = (self->png)
                ? gdk_texture_new_from_bytes (self->png, &error)
                : gdk_texture_new_from_filename (self->cache_path, &error);
            if (error)
                g_warning ("Failed to load image from %s: %s", source, error->message);
            if (!self->checksum)
                self->checksum = g_paste_image_item_compute_checksum (self->image);
        }
        break;
    }

    g_paste_image_item_set_size (item);
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
    GPasteImageItem *self = G_PASTE_IMAGE_ITEM (object);

    g_free (self->cache_path);
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
_g_paste_image_item_new (const gchar *cache_path,
                         GDateTime   *date,
                         GdkTexture  *image,
                         gchar       *checksum)
{
    /* An image item's value is its checksum: what says *which* image this is,
     * wherever its bytes happen to live. An item being loaded off a file has
     * none yet -- activating it below is what computes one -- so the value
     * starts empty and is set as soon as there is one. */
    GPasteItem *item = g_paste_item_new (G_PASTE_TYPE_IMAGE_ITEM, (checksum) ? checksum : "");
    GPasteImageItem *self = G_PASTE_IMAGE_ITEM (item);

    self->cache_path = g_strdup (cache_path);
    self->date = date;
    self->image = image;
    self->checksum = checksum; /* may be NULL, takes ownership */

    if (image)
    {
        if (!self->checksum)
            self->checksum = g_paste_image_item_compute_checksum (image);
    }
    else
        g_paste_image_item_set_state (item, G_PASTE_ITEM_STATE_ACTIVE);

    if (!self->image || !GDK_IS_TEXTURE (self->image))
    {
        g_object_unref (item);
        return NULL;
    }

    g_paste_item_set_value (item, self->checksum);

    /* Translators: strftime format for image timestamps. Rearrange to match your locale's date/time convention. */
    g_autofree gchar *formatted_date = g_date_time_format (date, _("%m/%d/%y %T"));
    /* Translators: an image item's dimensions and capture date, shown in history
     * behind a "[Image, ...]" the drawing client puts around it. %1$d is the
     * width, %2$d the height, %3$s the formatted date; reorder them freely. */
    g_autofree gchar *display_string = g_strdup_printf (_("%1$d × %2$d (%3$s)"),
                                                        gdk_texture_get_width (self->image),
                                                        gdk_texture_get_height (self->image),
                                                        formatted_date);
    g_paste_item_set_display_string (item, g_steal_pointer (&display_string));

    if (image)
        g_paste_image_item_set_size (item);
    else
        g_paste_image_item_set_state (item, G_PASTE_ITEM_STATE_IDLE);

    return item;
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

    /* No cache path: a capture writes nothing, and where its bytes end up --
     * a file, a database blob, nowhere at all -- is the storage backend's to
     * decide once the item is persisted. */
    GPasteItem *self = _g_paste_image_item_new (NULL,
                                                g_date_time_new_now_local (),
                                                g_object_ref (texture),
                                                g_paste_image_item_compute_checksum (texture));
    if (!self)
        return NULL;

    /* Encode once and carry the PNG: persisting the image (as a database blob,
     * a plain cache file, an encrypted side file...) is the storage backend's
     * business, and D-Bus clients get the bytes without touching the disk. */
    g_paste_image_item_take_png (self, gdk_texture_save_to_png_bytes (texture));

    return self;
}

static GPasteItem *
_g_paste_image_item_new_from_bytes (const gchar *cache_path,
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

    GPasteItem *self = _g_paste_image_item_new (cache_path,
                                                g_date_time_ref (date),
                                                texture,
                                                (checksum) ? g_strdup (checksum) : g_paste_image_item_compute_checksum (texture));

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
 * @png: the encoded PNG we want to be contained in the #GPasteImageItem
 * @date: (transfer none): the date at which the image was created
 * @checksum: (nullable): the image's known SHA256 checksum, or %NULL to compute it
 *
 * Create a new instance of #GPasteImageItem from its encoded bytes (e.g. a
 * storage backend's blob), touching no file at all: the item carries the image
 * and has no cache path, because nothing on disk holds it.
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

    return _g_paste_image_item_new_from_bytes (NULL, png, date, checksum);
}

/**
 * g_paste_image_item_new_from_bytes_at_path:
 * @path: the on-disk location the image was stored with
 * @png: the encoded PNG we want to be contained in the #GPasteImageItem
 * @date: (transfer none): the date at which the image was created
 * @checksum: (nullable): the image's known SHA256 checksum, or %NULL to compute it
 *
 * Like g_paste_image_item_new_from_bytes() but for bytes that came out of a
 * file (e.g. the encrypted file backend reading an image side file): the item
 * knows where its materialized data lives, and the backend can find it again
 * without going through this item's value.
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

    return _g_paste_image_item_new_from_bytes (path, png, date, checksum);
}

/**
 * g_paste_image_item_new_from_file:
 * @path: the file holding the image we want to be contained in the #GPasteImageItem
 * @date: (transfer none): the date at which the image was created
 * @checksum: (nullable): the image's known SHA256 checksum, or %NULL to compute it
 *
 * Create a new instance of #GPasteImageItem from a file, which becomes the
 * item's cache path: the image is read from it now (that is what computes the
 * checksum when @checksum is %NULL) and again whenever the item is activated.
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
