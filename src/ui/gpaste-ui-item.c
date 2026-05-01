/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <sys/stat.h>

#include <gpaste/gpaste-util.h>

#include <gpaste-ui-item.h>

struct _GPasteUiItem
{
    GPasteUiItemSkeleton parent_instance;
};

typedef struct
{
    GPasteClient   *client;
    GPasteSettings *settings;

    GtkWindow      *rootwin;

    guint64         index;
    gboolean        fake_index;
    gchar          *uuid;

    guint64         size_id;
} GPasteUiItemPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UiItem, ui_item, G_PASTE_TYPE_UI_ITEM_SKELETON)

/**
 * g_paste_ui_item_activate:
 * @self: a #GPasteUiItem instance
 *
 * Activate/Select the item
 *
 * returns: whether there was anything to select or not
 */
G_PASTE_VISIBLE gboolean
g_paste_ui_item_activate (GPasteUiItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_UI_ITEM (self), FALSE);

    const GPasteUiItemPrivate *priv = _g_paste_ui_item_get_instance_private (self);

    if (!priv->uuid)
        return FALSE;

    g_paste_client_select (priv->client, priv->uuid, NULL, NULL);

    if (g_paste_settings_get_close_on_select (priv->settings))
        gtk_window_close (priv->rootwin); /* Exit the application */

    return TRUE;
}

typedef struct
{
    char   *filename;
    int     width;
    int     height;
    time_t  last_modified;
} ImageInfo;

static GHashTable *image_cache = NULL;

static void
initialize_image_cache (void)
{
    if (image_cache != NULL)
        return;

    image_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

static void
update_image_cache (const gchar *path, int width, int height, time_t mtime)
{
    if (!image_cache)
        initialize_image_cache ();

    ImageInfo *info = g_new0 (ImageInfo, 1);
    info->filename = g_strdup (path);
    info->width = width;
    info->height = height;
    info->last_modified = mtime;

    gchar *key = g_strdup_printf ("%dx%d", width, height);
    g_hash_table_replace (image_cache, key, info);
}

static ImageInfo *
find_image_by_dimensions (int width, int height)
{
    if (!image_cache)
        return NULL;

    gchar *key = g_strdup_printf ("%dx%d", width, height);
    ImageInfo *info = g_hash_table_lookup (image_cache, key);
    g_free (key);

    if (info)
    {
        struct stat st;
        if (stat (info->filename, &st) == 0 && st.st_mtime == info->last_modified)
            return info;
        return NULL;
    }

    return NULL;
}

static gchar *
find_most_recent_image (const gchar *images_dir)
{
    GError *error = NULL;
    GDir *dir = g_dir_open (images_dir, 0, &error);

    if (error)
    {
        g_error_free (error);
        return NULL;
    }

    const gchar *filename;
    gchar *most_recent_file = NULL;
    time_t most_recent_time = 0;

    while ((filename = g_dir_read_name (dir)))
    {
        if (!g_str_has_suffix (filename, ".png"))
            continue;

        g_autofree gchar *path = g_build_filename (images_dir, filename, NULL);
        struct stat st;

        if (stat (path, &st) == 0 && st.st_mtime > most_recent_time)
        {
            g_free (most_recent_file);
            most_recent_file = g_strdup (path);
            most_recent_time = st.st_mtime;
        }
    }

    g_dir_close (dir);
    return most_recent_file;
}

static void
g_paste_ui_item_try_load_image (GPasteUiItem *self, const gchar *data)
{
    if (!data || !*data)
        return;

    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);

    if (!g_paste_settings_get_images_preview (priv->settings))
        return;

    if (!g_str_has_prefix (data, "[Image, ") || !g_str_has_suffix (data, ")]"))
        return;

    GPasteUiItemSkeleton *sk = G_PASTE_UI_ITEM_SKELETON (self);
    gint width = 0, height = 0;
    int date_year = 0, date_month = 0, date_day = 0;
    int hour = 0, minute = 0, second = 0;

    sscanf (data, "[Image, %d x %d (%d/%d/%d %d:%d:%d)]",
            &width, &height, &date_month, &date_day, &date_year, &hour, &minute, &second);

    ImageInfo *cached_info = find_image_by_dimensions (width, height);
    if (cached_info)
    {
        GError *error = NULL;
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file (cached_info->filename, &error);

        if (!error && pixbuf)
        {
            g_paste_ui_item_skeleton_set_thumbnail (sk, pixbuf);
            g_object_unref (pixbuf);
            return;
        }

        g_clear_error (&error);
    }

    g_autofree gchar *images_dir = g_build_filename (g_get_user_data_dir (), "gpaste", "images", NULL);

    if (width > 0 && height > 0)
    {
        GError *error = NULL;
        GDir *dir = g_dir_open (images_dir, 0, &error);

        if (error)
        {
            g_error_free (error);
        }
        else
        {
            const gchar *filename;
            GdkPixbuf *found_pixbuf = NULL;

            while ((filename = g_dir_read_name (dir)))
            {
                if (!g_str_has_suffix (filename, ".png"))
                    continue;

                g_autofree gchar *path = g_build_filename (images_dir, filename, NULL);
                struct stat st;
                time_t mtime = (stat (path, &st) == 0) ? st.st_mtime : 0;

                GError *img_error = NULL;
                GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file (path, &img_error);

                if (!img_error && pixbuf)
                {
                    int img_width = gdk_pixbuf_get_width (pixbuf);
                    int img_height = gdk_pixbuf_get_height (pixbuf);

                    update_image_cache (path, img_width, img_height, mtime);

                    if (img_width == width && img_height == height)
                    {
                        found_pixbuf = pixbuf;
                        break;
                    }

                    g_object_unref (pixbuf);
                }

                g_clear_error (&img_error);
            }

            g_dir_close (dir);

            if (found_pixbuf)
            {
                g_paste_ui_item_skeleton_set_thumbnail (sk, found_pixbuf);
                g_object_unref (found_pixbuf);
                return;
            }
        }
    }

    g_autofree gchar *recent_image = find_most_recent_image (images_dir);
    if (recent_image)
    {
        GError *error = NULL;
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file (recent_image, &error);

        if (!error && pixbuf)
        {
            int img_width = gdk_pixbuf_get_width (pixbuf);
            int img_height = gdk_pixbuf_get_height (pixbuf);

            g_paste_ui_item_skeleton_set_thumbnail (sk, pixbuf);
            g_object_unref (pixbuf);

            struct stat st;
            if (stat (recent_image, &st) == 0)
                update_image_cache (recent_image, img_width, img_height, st.st_mtime);
        }

        g_clear_error (&error);
    }
}

static void
g_paste_ui_item_on_kind_ready (GObject      *source_object G_GNUC_UNUSED,
                               GAsyncResult *res,
                               gpointer      user_data)
{
    GPasteUiItem *self = user_data;
    const GPasteUiItemPrivate *priv = _g_paste_ui_item_get_instance_private (self);
    g_autoptr (GError) error = NULL;
    GPasteItemKind kind = g_paste_client_get_element_kind_finish (priv->client, res, &error);

    if (error)
        return;

    GPasteUiItemSkeleton *sk = G_PASTE_UI_ITEM_SKELETON (self);

    g_paste_ui_item_skeleton_set_editable (sk, kind == G_PASTE_ITEM_KIND_TEXT);
    g_paste_ui_item_skeleton_set_uploadable (sk, kind == G_PASTE_ITEM_KIND_TEXT);

    if (kind != G_PASTE_ITEM_KIND_IMAGE)
        g_paste_ui_item_skeleton_set_thumbnail (sk, NULL);
}

static void
_g_paste_ui_item_ready (GPasteUiItem *self,
                        const gchar  *txt)
{
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);
    g_autofree gchar *oneline = g_paste_util_replace (txt, "\n", " ");

    g_paste_client_get_element_kind (priv->client, priv->uuid, g_paste_ui_item_on_kind_ready, self);
    g_paste_ui_item_skeleton_set_index_and_uuid (G_PASTE_UI_ITEM_SKELETON (self), priv->index, priv->uuid);

    if (!priv->index)
    {
        g_autofree gchar *markup = g_markup_printf_escaped ("<b>%s</b>", oneline);
        g_paste_ui_item_skeleton_set_markup (G_PASTE_UI_ITEM_SKELETON (self), markup);
    }
    else
    {
        g_paste_ui_item_skeleton_set_text (G_PASTE_UI_ITEM_SKELETON (self), oneline);
    }

    g_paste_ui_item_try_load_image (self, txt);
}

static void
g_paste_ui_item_on_text_ready (GObject      *source_object G_GNUC_UNUSED,
                               GAsyncResult *res,
                               gpointer      user_data)
{
    GPasteUiItem *self = user_data;
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);
    g_autoptr (GError) error = NULL;
    g_autofree gchar *txt = g_paste_client_get_element_finish (priv->client, res, &error);

    if (!txt || error)
        return;

    _g_paste_ui_item_ready (self, txt);
}

static void
g_paste_ui_item_on_item_ready (GObject      *source_object G_GNUC_UNUSED,
                               GAsyncResult *res,
                               gpointer      user_data)
{
    GPasteUiItem *self = user_data;
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClientItem) txt = g_paste_client_get_element_at_index_finish (priv->client, res, &error);

    if (!txt || error)
        return;

    g_autofree gchar *uuid = priv->uuid;
    priv->uuid = g_strdup (g_paste_client_item_get_uuid (txt));

    _g_paste_ui_item_ready (self, g_paste_client_item_get_value (txt));
}

static void
g_paste_ui_item_reset_text (GPasteUiItem *self)
{
    g_return_if_fail (_G_PASTE_IS_UI_ITEM (self));

    const GPasteUiItemPrivate *priv = _g_paste_ui_item_get_instance_private (self);

    if (priv->fake_index)
        g_paste_client_get_element (priv->client, priv->uuid, g_paste_ui_item_on_text_ready, self);
    else
        g_paste_client_get_element_at_index (priv->client, priv->index, g_paste_ui_item_on_item_ready, self);
}

/**
 * g_paste_ui_item_refresh:
 * @self: a #GPasteUiItem instance
 *
 * Refresh the item
 */
G_PASTE_VISIBLE void
g_paste_ui_item_refresh (GPasteUiItem *self)
{
    g_return_if_fail (_G_PASTE_IS_UI_ITEM (self));

    g_paste_ui_item_reset_text (self);
}

static void
_g_paste_ui_item_set_index (GPasteUiItem *self,
                            guint64       index,
                            gboolean      fake_index)
{
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);

    priv->index = index;
    priv->fake_index = fake_index;

    if (index != (guint64) -1)
    {
        g_paste_ui_item_reset_text (self);
        gtk_widget_show (GTK_WIDGET (self));
    }
    else if (priv->uuid)
    {
        gtk_widget_hide (GTK_WIDGET (self));
    }
}

/**
 * g_paste_ui_item_set_index:
 * @self: a #GPasteUiItem instance
 * @index: the index of the corresponding item
 *
 * Track a new index
 */
G_PASTE_VISIBLE void
g_paste_ui_item_set_index (GPasteUiItem *self,
                           guint64       index)
{
    g_return_if_fail (_G_PASTE_IS_UI_ITEM (self));

    _g_paste_ui_item_set_index (self, index, FALSE);
}

/**
 * g_paste_ui_item_set_uuid:
 * @self: a #GPasteUiItem instance
 * @uuid: the uuid of the corresponding item
 *
 * Track a new uuid
 */
G_PASTE_VISIBLE void
g_paste_ui_item_set_uuid (GPasteUiItem *self,
                          const gchar  *uuid)
{
    g_return_if_fail (_G_PASTE_IS_UI_ITEM (self));

    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);
    g_autofree gchar *_uuid = priv->uuid;

    priv->uuid = g_strdup (uuid);

    _g_paste_ui_item_set_index (self, (guint64) -2, TRUE);
}

static void
g_paste_ui_item_dispose (GObject *object)
{
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (G_PASTE_UI_ITEM (object));

    g_clear_object (&priv->client);
    g_clear_object (&priv->settings);
    g_clear_pointer (&priv->uuid, g_free);

    G_OBJECT_CLASS (g_paste_ui_item_parent_class)->dispose (object);
}

static void
g_paste_ui_item_class_init (GPasteUiItemClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_item_dispose;
}

static void
g_paste_ui_item_init (GPasteUiItem *self)
{
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (G_PASTE_UI_ITEM (self));

    priv->index = (guint64) -1;
}

/**
 * g_paste_ui_item_new:
 * @client: a #GPasteClient instance
 * @settings: a #GPasteSettings instance
 * @rootwin: the root #GtkWindow
 * @index: the index of the corresponding item
 *
 * Create a new instance of #GPasteUiItem
 *
 * Returns: a newly allocated #GPasteUiItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_item_new (GPasteClient   *client,
                     GPasteSettings *settings,
                     GtkWindow      *rootwin,
                     guint64         index)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = g_paste_ui_item_skeleton_new (G_PASTE_TYPE_UI_ITEM, client, settings, rootwin);
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (G_PASTE_UI_ITEM (self));

    priv->client = g_object_ref (client);
    priv->settings = g_object_ref (settings);
    priv->rootwin = rootwin;

    g_paste_ui_item_set_index (G_PASTE_UI_ITEM (self), index);

    return self;
}
