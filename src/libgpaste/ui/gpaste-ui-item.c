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

#include "gpaste-ui-item-private.h"

#include <glib/gi18n-lib.h>

struct _GPasteUiItemPrivate
{
    GPasteClient   *client;
    GPasteSettings *settings;

    GtkLabel       *label;
    guint32         index;

    gulong          size_id;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiItem, g_paste_ui_item, GTK_TYPE_LIST_BOX_ROW)

/* TODO: move me somewhere ( dupe from history ) */
static gchar *
g_paste_ui_item_replace (const gchar *text,
                         const gchar *pattern,
                         const gchar *substitution)
{
    G_PASTE_CLEANUP_FREE gchar *regex_string = g_regex_escape_string (pattern, -1);
    G_PASTE_CLEANUP_REGEX_UNREF GRegex *regex = g_regex_new (regex_string,
                                                             0, /* Compile options */
                                                             0, /* Match options */
                                                             NULL); /* Error */
    return g_regex_replace_literal (regex,
                                    text,
                                    (gssize) -1,
                                    0, /* Start position */
                                    substitution,
                                    0, /* Match options */
                                    NULL); /* Error */
}

static void
g_paste_ui_item_on_text_ready (GObject      *source_object G_GNUC_UNUSED,
                               GAsyncResult *res,
                               gpointer      user_data)
{
    GPasteUiItemPrivate *priv = user_data;
    if (!G_PASTE_IS_CLIENT (priv->client))
        return;
    G_PASTE_CLEANUP_ERROR_FREE GError *error = NULL;
    G_PASTE_CLEANUP_FREE gchar *txt = g_paste_client_get_element_finish (priv->client, res, &error);
    if (!txt || error)
        return;

    gtk_label_set_text (priv->label, g_paste_ui_item_replace (txt, "\n", ""));
}

/**
 * g_paste_ui_item_reset_text:
 * @self: a #GPasteUiItem instance
 *
 * Reset the text of the #GPasteUiItem
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_item_reset_text (GPasteUiItem *self)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM (self));

    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);

    g_paste_client_get_element (priv->client, priv->index, g_paste_ui_item_on_text_ready, priv);
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
g_paste_ui_item_activate (GtkListBoxRow *row)
{
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private ((GPasteUiItem *) row);
    g_paste_client_select (priv->client, priv->index, NULL, NULL);
}

static void
g_paste_ui_item_dispose (GObject *object)
{
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private ((GPasteUiItem *) object);

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
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_ui_item_dispose;
    GTK_LIST_BOX_ROW_CLASS (klass)->activate = g_paste_ui_item_activate;
}

static void
g_paste_ui_item_init (GPasteUiItem *self)
{
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);

    GtkWidget *label = gtk_label_new ("");
    priv->label = GTK_LABEL (label);

    GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
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
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private ((GPasteUiItem *) self);

    priv->client = g_object_ref (client);
    priv->settings = g_object_ref (settings);
    priv->index = index;

    gtk_label_set_ellipsize (priv->label, PANGO_ELLIPSIZE_END);
    gtk_box_pack_end (GTK_BOX (gtk_bin_get_child (GTK_BIN (self))), g_paste_ui_delete_new (client, index), FALSE, TRUE, 0);

    priv->size_id = g_signal_connect (settings,
                                      "changed::" G_PASTE_ELEMENT_SIZE_SETTING,
                                      G_CALLBACK (g_paste_ui_item_set_text_size),
                                      priv);
    g_paste_ui_item_set_text_size (settings, NULL, priv);
    g_paste_ui_item_reset_text ((GPasteUiItem *) self);

    return self;
}
