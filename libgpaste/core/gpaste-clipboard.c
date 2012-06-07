/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2012 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-clipboard-common.h"

#include <string.h>

#define G_PASTE_CLIPBOARD_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_CLIPBOARD, GPasteClipboardPrivate))

G_DEFINE_TYPE (GPasteClipboard, g_paste_clipboard, G_TYPE_OBJECT)

struct _GPasteClipboardPrivate
{
    GdkAtom target;
    GtkClipboard *real;
    GPasteSettings *settings;
    gchar *text;
    gchar *image_checksum;
};

/**
 * g_paste_clipboard_get_target:
 * @self: a #GPasteClipboard instance
 *
 * Get the target the #GPasteClipboard points to
 *
 * Returns: (transfer none): the GdkAtom representing the target (Primary, Clipboard, ...)
 */
G_PASTE_VISIBLE GdkAtom
g_paste_clipboard_get_target (const GPasteClipboard *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD (self), NULL);

    return self->priv->target;
}

/**
 * g_paste_clipboard_get_real:
 * @self: a #GPasteClipboard instance
 *
 * Get the GtkClipboard linked to the #GPasteClipboard
 *
 * Returns: (transfer none): the GtkClipboard used in the #GPasteClipboard
 */
G_PASTE_VISIBLE GtkClipboard *
g_paste_clipboard_get_real (const GPasteClipboard *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD (self), NULL);

    return self->priv->real;
}

/**
 * g_paste_clipboard_get_text:
 * @self: a #GPasteClipboard instance
 *
 * Get the text stored in the #GPasteClipboard
 *
 * Returns: read-only string containing the text or NULL
 */
G_PASTE_VISIBLE const gchar *
g_paste_clipboard_get_text (const GPasteClipboard *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD (self), NULL);

    return self->priv->text;
}

static void
_g_paste_clipboard_set_text (GPasteClipboard *self,
                             const gchar     *text)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARD (self));

    GPasteClipboardPrivate *priv = self->priv;

    g_free (priv->text);
    g_free (priv->image_checksum);

    priv->text = g_strdup (text);
    priv->image_checksum = NULL;
}

/**
 * g_paste_clipboard_set_text:
 * @self: a #GPasteClipboard instance
 *
 * Put the text from the intern GtkClipboard in the #GPasteClipboard
 *
 * Returns: The new text if it was modified, or NULL
 */
G_PASTE_VISIBLE const gchar *
g_paste_clipboard_set_text (GPasteClipboard *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD (self), NULL);

    GPasteClipboardPrivate *priv = self->priv;
    gchar *text = gtk_clipboard_wait_for_text (priv->real);

    if (!text)
        return NULL;

    GPasteSettings *settings = priv->settings;
    gchar *stripped = g_strstrip (g_strdup (text));
    gboolean trim_items = g_paste_settings_get_trim_items (settings);
    const gchar *to_add = trim_items ? stripped : text;
    const gchar *ret = NULL;
    gsize length = strlen (to_add);

    if (length < g_paste_settings_get_min_text_item_size (settings) ||
        length > g_paste_settings_get_max_text_item_size (settings) ||
        strlen (stripped) == 0 ||
        (priv->text &&
            g_strcmp0 (priv->text, to_add) == 0))
                goto ignore;

    if (trim_items &&
        priv->target == GDK_SELECTION_CLIPBOARD &&
        g_strcmp0 (text, stripped) != 0)
            g_paste_clipboard_select_text (self, stripped);
    else
        _g_paste_clipboard_set_text (self, to_add);

    ret = priv->text;

ignore:
    g_free (stripped);
    g_free (text);

    return ret;
}

/**
 * g_paste_clipboard_select_text:
 * @self: a #GPasteClipboard instance
 * @text: the text to select
 *
 * Put the text into the #GPasteClipbaord and the intern GtkClipboard
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_clipboard_select_text (GPasteClipboard *self,
                               const gchar     *text)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARD (self));
    g_return_if_fail (text != NULL);
    g_return_if_fail (g_utf8_validate (text, -1, NULL));

    GtkClipboard *real = self->priv->real;

    _g_paste_clipboard_set_text (self, text);
    gtk_clipboard_set_text (real, text, -1);
    gtk_clipboard_store (real);
}

static void
_g_paste_clipboard_select_uris (GPasteClipboard *self,
                                GPasteUrisItem  *item)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARD (self));
    g_return_if_fail (G_PASTE_IS_URIS_ITEM (item));

    GtkClipboard *real = self->priv->real;
    GtkTargetList *target_list = gtk_target_list_new (NULL, 0);

    _g_paste_clipboard_set_text (self, g_paste_item_get_value (G_PASTE_ITEM (item)));

    gtk_target_list_add_text_targets (target_list, 0);
    gtk_target_list_add_uri_targets (target_list, 0);
    gtk_target_list_add (target_list, g_paste_clipboard_copy_files_target, 0, 0);

    gint n_targets;
    GtkTargetEntry *targets = gtk_target_table_new_from_list (target_list, &n_targets);
    gtk_clipboard_set_with_owner (real,
                                  targets,
                                  n_targets,
                                  g_paste_clipboard_get_clipboard_data,
                                  g_paste_clipboard_clear_clipboard_data,
                                  g_object_ref (item));
    gtk_clipboard_store (real);

    gtk_target_table_free (targets, n_targets);
    gtk_target_list_unref (target_list);
}

/**
 * g_paste_clipboard_get_image_checksum:
 * @self: a #GPasteClipboard instance
 *
 * Get the checksum of the image stored in the #GPasteClipboard
 *
 * Returns: read-only string containing the checksum of the image stored in the #GPasteClipboard or NULL
 */
G_PASTE_VISIBLE const gchar *
g_paste_clipboard_get_image_checksum (const GPasteClipboard *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD (self), NULL);

    return self->priv->image_checksum;
}

static void
g_paste_clipboard_set_image_checksum (GPasteClipboard *self,
                                      const gchar     *image_checksum)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARD (self));

    GPasteClipboardPrivate *priv = self->priv;

    g_free (priv->text);
    g_free (priv->image_checksum);

    priv->text = NULL;
    priv->image_checksum = g_strdup (image_checksum);
}

static void
_g_paste_clipboard_select_image (GPasteClipboard *self,
                                 GdkPixbuf       *image,
                                 const gchar     *checksum)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARD (self));
    g_return_if_fail (image != NULL);

    GtkClipboard *real = self->priv->real;

    g_paste_clipboard_set_image_checksum (self, checksum);
    gtk_clipboard_set_image (real, image);
    //gtk_clipboard_store (real);
}

/**
 * g_paste_clipboard_set_image:
 * @self: a #GPasteClipboard instance
 *
 * Put the image from the intern GtkClipboard in the #GPasteClipboard
 *
 * Returns: (transfer full): The new image if it was modified, or NULL
 */
G_PASTE_VISIBLE GdkPixbuf *
g_paste_clipboard_set_image (GPasteClipboard *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD (self), NULL);

    GPasteClipboardPrivate *priv = self->priv;
    GdkPixbuf *image = gtk_clipboard_wait_for_image (priv->real), *ret = image;

    if (image)
    {
        gchar *checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA256,
                                                       (guchar *) gdk_pixbuf_get_pixels (image),
                                                       -1);

        if (g_strcmp0 (checksum, self->priv->image_checksum) != 0)
            _g_paste_clipboard_select_image (self,
                                             image,
                                             checksum);
        else
            ret = NULL;

        g_free (checksum);
    }

    return ret;
}

/**
 * g_paste_clipboard_select_item:
 * @self: a #GPasteClipboard instance
 * @item: the item to select
 *
 * Put the value of the item into the #GPasteClipbaord and the intern GtkClipboard
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_clipboard_select_item (GPasteClipboard  *self,
                               const GPasteItem *item)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARD (self));
    g_return_if_fail (G_PASTE_IS_ITEM (item));

    if (G_PASTE_IS_IMAGE_ITEM (item))
    {
        GPasteImageItem *image_item = G_PASTE_IMAGE_ITEM (item);
        const gchar *checksum = g_paste_image_item_get_checksum (image_item);

        if (g_strcmp0 (checksum, self->priv->image_checksum) != 0)
            _g_paste_clipboard_select_image (self,
                                             g_paste_image_item_get_image (image_item),
                                             checksum);
    }
    else
    {
        const gchar *text = g_paste_item_get_value (item);

        if (g_strcmp0 (text, self->priv->text) != 0)
        {
            if (G_PASTE_IS_URIS_ITEM (item))
                _g_paste_clipboard_select_uris (self, G_PASTE_URIS_ITEM (item));
            else /* if (G_PASTE_IS_TEXT_ITEM (item)) */
                g_paste_clipboard_select_text (self, text);
        }
    }
}

static void
g_paste_clipboard_dispose (GObject *object)
{
    GPasteClipboardPrivate *priv = G_PASTE_CLIPBOARD (object)->priv;

    g_object_unref (priv->settings);

    G_OBJECT_CLASS (g_paste_clipboard_parent_class)->dispose (object);
}

static void
g_paste_clipboard_finalize (GObject *object)
{
    GPasteClipboardPrivate *priv = G_PASTE_CLIPBOARD (object)->priv;

    g_free (priv->text);
    g_free (priv->image_checksum);

    G_OBJECT_CLASS (g_paste_clipboard_parent_class)->finalize (object);
}

static void
g_paste_clipboard_class_init (GPasteClipboardClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPasteClipboardPrivate));

    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_clipboard_dispose;
    object_class->finalize = g_paste_clipboard_finalize;
}

static void
g_paste_clipboard_init (GPasteClipboard *self)
{
    GPasteClipboardPrivate *priv = self->priv = G_PASTE_CLIPBOARD_GET_PRIVATE (self);

    priv->text = NULL;
    priv->image_checksum = NULL;
}

/**
 * g_paste_clipboard_new:
 * @target: the GdkAtom representating the GtkClipboard we're abstracting
 * @settings: a #GPasteSettings instance
 *
 * Create a new instance of #GPasteClipboard
 *
 * Returns: a newly allocated #GPasteClipboard
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClipboard *
g_paste_clipboard_new (GdkAtom         target,
                       GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    GPasteClipboard *self = g_object_new (G_PASTE_TYPE_CLIPBOARD, NULL);
    GPasteClipboardPrivate *priv = self->priv;

    priv->target = target;
    priv->real = gtk_clipboard_get (target);
    priv->settings = g_object_ref (settings);

    return self;
}
