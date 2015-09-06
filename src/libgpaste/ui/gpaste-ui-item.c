/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include <gpaste-gsettings-keys.h>
#include <gpaste-ui-delete.h>
#include <gpaste-ui-edit.h>
#include <gpaste-util.h>

struct _GPasteUiItem
{
    GtkListBoxRow parent_instance;
};

typedef struct
{
    GPasteClient   *client;
    GPasteSettings *settings;
    GPasteUiDelete *delete;

    GtkLabel       *label;
    guint32         index;
    gboolean        bold;

    gulong          size_id;
} GPasteUiItemPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiItem, g_paste_ui_item, GTK_TYPE_LIST_BOX_ROW)

/**
 * g_paste_ui_item_activate:
 * @self: a #GPasteUiItem instance
 *
 * Refresh the item
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_item_activate (GPasteUiItem *self)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM (self));
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);

    g_paste_client_select (priv->client, priv->index, NULL, NULL);
}

/**
 * g_paste_ui_item_refresh:
 * @self: a #GPasteUiItem instance
 *
 * Refresh the item
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_item_refresh (GPasteUiItem *self)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM (self));
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);

    g_paste_ui_item_set_index (self, priv->index);
}

static void
g_paste_ui_item_on_text_ready (GObject      *source_object G_GNUC_UNUSED,
                               GAsyncResult *res,
                               gpointer      user_data)
{
    GPasteUiItemPrivate *priv = user_data;
    if (!G_PASTE_IS_CLIENT (priv->client))
        return;
    g_autoptr (GError) error = NULL;
    g_autofree gchar *txt = g_paste_client_get_element_finish (priv->client, res, &error);
    if (!txt || error)
        return;
    g_autofree gchar *oneline = g_paste_util_replace (txt, "\n", "");

    if (priv->bold)
    {
        g_autofree gchar *markup = g_markup_printf_escaped ("<b>%s</b>", oneline);
        gtk_label_set_markup (priv->label, markup);
    }
    else
        gtk_label_set_text (priv->label, oneline);
}

static void
g_paste_ui_item_reset_text (GPasteUiItem *self)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM (self));
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);

    g_paste_client_get_element (priv->client, priv->index, g_paste_ui_item_on_text_ready, priv);
}

/**
 * g_paste_ui_item_set_index:
 * @self: a #GPasteUiItem instance
 * @index: the index of the corresponding item
 *
 * Track a new index
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_item_set_index (GPasteUiItem *self,
                           guint32       index)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM (self));
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);

    guint32 old_index = priv->index;
    priv->index = index;

    if (!index)
        priv->bold = TRUE;
    else if (!old_index)
        priv->bold = FALSE;

    g_paste_ui_delete_set_index (priv->delete, index);

    if (index != (guint32)-1)
    {
        g_paste_ui_item_reset_text (self);
        gtk_widget_show (GTK_WIDGET (self));
    }
    else
    {
        gtk_label_set_text (priv->label, "");
        gtk_widget_hide (GTK_WIDGET (self));
    }
}

/**
 * g_paste_ui_item_get_text:
 * @self: a #GPasteUiItem instance
 *
 * Get the currently displayed text
 *
 * Returns:
 */
G_PASTE_VISIBLE const gchar *
g_paste_ui_item_get_text (const GPasteUiItem *self)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM (self));
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);

    return gtk_label_get_text (priv->label);
}

static void
g_paste_ui_item_set_text_size (GPasteSettings *settings,
                               const gchar    *key G_GNUC_UNUSED,
                               gpointer        user_data)
{
    GPasteUiItemPrivate *priv = user_data;
    gtk_label_set_max_width_chars (priv->label, g_paste_settings_get_element_size (settings));
}

static void
g_paste_ui_item_dispose (GObject *object)
{
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (G_PASTE_UI_ITEM (object));

    g_clear_object (&priv->client);
    if (priv->settings)
    {
        g_signal_handler_disconnect (priv->settings, priv->size_id);
        g_clear_object (&priv->settings);
    }

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
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);

    GtkWidget *label = gtk_label_new ("");
    priv->label = GTK_LABEL (label);
    priv->index = (guint32)-1;

    gtk_widget_set_margin_start (label, 5);
    gtk_widget_set_margin_end (label, 5);
    gtk_label_set_ellipsize (priv->label, PANGO_ELLIPSIZE_END);
    gtk_widget_set_margin_top (label, 5);
    gtk_widget_set_margin_bottom (label, 5);

    GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_margin_start (hbox, 5);
    gtk_widget_set_margin_end (hbox, 5);
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);

    gtk_container_add (GTK_CONTAINER (self), hbox);
}

/**
 * g_paste_ui_item_new:
 * @client: a #GPasteClient instance
 * @settings: a #GPasteSettings instance
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
                     guint32         index)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_ITEM, "selectable", FALSE, NULL);
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (G_PASTE_UI_ITEM (self));
    GtkWidget *edit = g_paste_ui_edit_new (G_PASTE_UI_ITEM (self), client, index);
    GtkWidget *delete = g_paste_ui_delete_new (client, index);

    priv->client = g_object_ref (client);
    priv->settings = g_object_ref (settings);
    priv->delete = G_PASTE_UI_DELETE (delete);

    gtk_box_pack_end (GTK_BOX (gtk_bin_get_child (GTK_BIN (self))), delete, FALSE, TRUE, 0);
    gtk_box_pack_end (GTK_BOX (gtk_bin_get_child (GTK_BIN (self))), edit, FALSE, TRUE, 0);

    priv->size_id = g_signal_connect (settings,
                                      "changed::" G_PASTE_ELEMENT_SIZE_SETTING,
                                      G_CALLBACK (g_paste_ui_item_set_text_size),
                                      priv);
    g_paste_ui_item_set_text_size (settings, NULL, priv);
    g_paste_ui_item_set_index (G_PASTE_UI_ITEM (self), index);

    return self;
}
