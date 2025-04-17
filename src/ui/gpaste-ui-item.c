/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste/gpaste-util.h>

#include <gpaste-ui-item.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>

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



static gboolean
is_image_file_path (const gchar *path)
{
    if (!path || !*path)
        return FALSE;

    /* Check if the path has an image extension, even if it doesn't exist yet */
    const gchar *extensions[] = {
        ".png", ".jpg", ".jpeg", ".gif", ".bmp",
        ".tiff", ".webp", ".svg", NULL
    };
    
    gchar *lowercase = g_ascii_strdown (path, -1);
    gboolean is_image = FALSE;
    
    for (const gchar **ext = extensions; *ext != NULL; ext++) {
        if (g_str_has_suffix (lowercase, *ext)) {
            is_image = TRUE;
            break;
        }
    }
    
    g_free (lowercase);
    return is_image;
}

static void
g_paste_ui_item_try_load_image (GPasteUiItem *self, const gchar *data)
{
    if (!data || !*data)
        return;

    /* For debugging, print to the console what we're trying to process */
    g_printerr ("Processing item: '%s'\n", data);

    /* Handle specific format seen: -/Pictures/screen.jpg */
    if (g_str_has_prefix (data, "-/")) {
        g_printerr ("Found -/ path format\n");
        
        /* Convert -/path to $HOME/path */
        g_autofree gchar *home_path = g_build_filename (g_get_home_dir(), data + 2, NULL);
        g_printerr ("Converted to home path: %s\n", home_path);
        
        if (is_image_file_path (home_path)) {
            g_printerr ("Path has image extension, trying to load\n");
            
            GError *error = NULL;
            GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file (home_path, &error);
            
            if (!error && pixbuf) {
                g_printerr ("Successfully loaded image from home path\n");
                g_paste_ui_item_skeleton_set_thumbnail (G_PASTE_UI_ITEM_SKELETON (self), pixbuf);
                g_object_unref (pixbuf);
                return;
            } else if (error) {
                g_printerr ("Error loading image from home path: %s\n", error->message);
                g_error_free (error);
            }
        }
    }
    
    /* Check if this is the formatted string for an image item */
    if (g_str_has_prefix (data, "[Image, ") && g_str_has_suffix (data, ")]")) {
        g_printerr ("Found image format entry\n");
        
        /* Extract dimensions if possible */
        int width = 0, height = 0;
        int date_year = 0, date_month = 0, date_day = 0;
        int hour = 0, minute = 0, second = 0;
        
        /* Parse the image string to extract dimensions and timestamp */
        sscanf (data, "[Image, %d x %d (%d/%d/%d %d:%d:%d)]", 
                &width, &height, &date_month, &date_day, &date_year, &hour, &minute, &second);
        
        /* Try to find the image file in the GPaste images directory */
        g_autofree gchar *images_dir_path = g_build_filename (g_get_user_data_dir (), "gpaste", "images", NULL);
        g_printerr ("Looking for images in: %s\n", images_dir_path);
        
        GError *error = NULL;
        GDir *dir = g_dir_open (images_dir_path, 0, &error);
        
        if (error) {
            g_printerr ("Error opening images directory: %s\n", error->message);
            g_error_free (error);
            return;
        }

        /* Loop through all .png files in the directory */
        const gchar *filename;
        gboolean found = FALSE;
        GdkPixbuf *pixbuf = NULL;

        while ((filename = g_dir_read_name (dir)) && !found) {
            if (g_str_has_suffix (filename, ".png")) {
                g_autofree gchar *file_path = g_build_filename (images_dir_path, filename, NULL);
                
                /* Try to load the pixbuf to check dimensions */
                error = NULL;
                pixbuf = gdk_pixbuf_new_from_file (file_path, &error);
                
                if (!error && pixbuf) {
                    int img_width = gdk_pixbuf_get_width (pixbuf);
                    int img_height = gdk_pixbuf_get_height (pixbuf);
                    
                    /* Check if dimensions match */
                    if (width == 0 || height == 0 || 
                        (img_width == width && img_height == height)) {
                        g_printerr ("Found matching image: %s (%dx%d)\n", file_path, img_width, img_height);
                        found = TRUE;
                    } else {
                        g_object_unref (pixbuf);
                        pixbuf = NULL;
                    }
                } else if (error) {
                    g_printerr ("Error loading image %s: %s\n", file_path, error->message);
                    g_error_free (error);
                }
            }
        }

        g_dir_close (dir);

        /* If we found a matching image, set it as thumbnail */
        if (found && pixbuf) {
            g_printerr ("Setting image thumbnail from [Image] entry\n");
            g_paste_ui_item_skeleton_set_thumbnail (G_PASTE_UI_ITEM_SKELETON (self), pixbuf);
            g_object_unref (pixbuf);
        } else {
            g_printerr ("No matching image found for dimensions %dx%d\n", width, height);
        }
    } else if (g_str_has_prefix (data, "[Files] ")) {
        g_printerr ("Found [Files] entry\n");
        
        /* Handle file references - commonly used when copying files in file managers */
        const gchar *file_path = data + 8; /* Skip "[Files] " prefix */
        g_printerr ("Extracted file path: '%s'\n", file_path);
        
        /* Try to extract the actual file path - sometimes the [Files] prefix is followed by
         * multiple newlines or spaces or other formatting */
        g_auto(GStrv) lines = g_strsplit (file_path, "\n", -1);
        
        /* Process each line - in case there are multiple files */
        for (gchar **line_ptr = lines; *line_ptr != NULL; line_ptr++) {
            gchar *line = g_strstrip (*line_ptr);
            if (!*line) continue; /* Skip empty lines */
            
            g_printerr ("Processing file path line: '%s'\n", line);
            
            /* Check if it's a file URL or a regular path */
            gchar *real_path = NULL;
            if (g_str_has_prefix (line, "file://")) {
                real_path = g_filename_from_uri (line, NULL, NULL);
                g_printerr ("Converted URI to path: '%s'\n", real_path ? real_path : "conversion failed");
            } else {
                real_path = g_strdup (line);
                g_printerr ("Using path directly: '%s'\n", real_path);
            }
            
            if (real_path) {
                g_printerr ("Checking if %s is an image file...\n", real_path);
                gboolean is_image = is_image_file_path (real_path);
                g_printerr ("Is image: %s\n", is_image ? "YES" : "NO");
                
                if (is_image) {
                    GError *error = NULL;
                    g_printerr ("Trying to load image from %s\n", real_path);
                    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file (real_path, &error);
                    
                    if (!error && pixbuf) {
                        g_printerr ("Successfully loaded image, setting thumbnail\n");
                        g_paste_ui_item_skeleton_set_thumbnail (G_PASTE_UI_ITEM_SKELETON (self), pixbuf);
                        g_object_unref (pixbuf);
                        g_free (real_path);
                        return; /* Successfully loaded one image, we're done */
                    } else if (error) {
                        g_printerr ("Error loading image: %s\n", error->message);
                        g_error_free (error);
                    }
                }
                g_free (real_path);
            } else {
                g_printerr ("Failed to get real path\n");
            }
        }
    } else if (g_file_test (data, G_FILE_TEST_EXISTS)) {
        g_printerr ("Found direct file path\n");
        
        /* Test if it's an image file */
        gboolean is_image = is_image_file_path (data);
        g_printerr ("Is image file: %s\n", is_image ? "YES" : "NO");
        
        if (is_image) {
            /* If data is a valid file path to an image, try to load it directly */
            GError *error = NULL;
            GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file (data, &error);
            
            if (error) {
                g_printerr ("Error loading direct file: %s\n", error->message);
                g_error_free (error);
                return;
            }
            
            if (pixbuf) {
                g_printerr ("Setting thumbnail from direct file path\n");
                g_paste_ui_item_skeleton_set_thumbnail (G_PASTE_UI_ITEM_SKELETON (self), pixbuf);
                g_object_unref (pixbuf);
            }
        }
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
    
    /* For non-image items, check if it might be a file path to an image */
    if (kind != G_PASTE_ITEM_KIND_IMAGE) {
        /* We'll try to detect file paths in the _g_paste_ui_item_ready function */
        g_paste_ui_item_skeleton_set_thumbnail (sk, NULL);
    }

    /* Note: For image items and potential image file paths, _g_paste_ui_item_ready 
     * will try to load the image based on the item's content
     */
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
    
    /* Try to load image if this is an image item */
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
