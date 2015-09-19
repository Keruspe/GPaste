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
#include <gpaste-ui-item-skeleton.h>

typedef struct
{
    GPasteSettings *settings;

    GtkLabel       *index_label;
    GtkLabel       *label;

    gulong          size_id;
} GPasteUiItemSkeletonPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GPasteUiItemSkeleton, g_paste_ui_item_skeleton, GTK_TYPE_LIST_BOX_ROW)

static void
g_paste_ui_item_skeleton_set_text_size (GPasteSettings *settings,
                                        const gchar    *key G_GNUC_UNUSED,
                                        gpointer        user_data)
{
    GPasteUiItemSkeletonPrivate *priv = user_data;
    guint32 size = g_paste_settings_get_element_size (settings);

    gtk_label_set_width_chars (priv->label, size);
    gtk_label_set_max_width_chars (priv->label, size);
}

/**
 * g_paste_ui_item_skeleton_set_text:
 * @self: the #GPasteUiItemSkeleton instance
 * @text: the new text for the label
 *
 * Changes the displayed text
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_item_skeleton_set_text (GPasteUiItemSkeleton *self,
                                   const gchar          *text)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM_SKELETON (self));
    g_return_val_if_fail (g_utf8_validate (text, -1, NULL), NULL);

    GPasteUiItemSkeletonPrivate *priv = g_paste_ui_item_skeleton_get_instance_private (self);

    gtk_label_set_text (priv->label, text);
}

/**
 * g_paste_ui_item_skeleton_set_markup:
 * @self: the #GPasteUiItemSkeleton instance
 * @markup: the new markup for the label
 *
 * Changes the displayed markup
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_item_skeleton_set_markup (GPasteUiItemSkeleton *self,
                                     const gchar          *markup)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM_SKELETON (self));
    g_return_val_if_fail (g_utf8_validate (markup, -1, NULL), NULL);

    GPasteUiItemSkeletonPrivate *priv = g_paste_ui_item_skeleton_get_instance_private (self);

    gtk_label_set_markup (priv->label, markup);
}

/**
 * g_paste_ui_item_skeleton_set_index:
 * @self: the #GPasteUiItemSkeleton instance
 * @index: the new index to display
 *
 * Changes the displayed index
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_item_skeleton_set_index (GPasteUiItemSkeleton *self,
                                    guint32               index)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM_SKELETON (self));

    GPasteUiItemSkeletonPrivate *priv = g_paste_ui_item_skeleton_get_instance_private (self);
    g_autofree gchar *_index = g_strdup_printf("%u", index);

    gtk_label_set_text (priv->index_label, _index);
}

/**
 * g_paste_ui_item_skeleton_get_label:
 * @self: the #GPasteUiItemSkeleton instance
 *
 * Get the inner label
 *
 * Returns: (transfer none): The inner #GtkLabel
 */
G_PASTE_VISIBLE GtkLabel *
g_paste_ui_item_skeleton_get_label (GPasteUiItemSkeleton *self)
{
    g_return_val_if_fail (G_PASTE_IS_UI_ITEM_SKELETON (self), NULL);

    GPasteUiItemSkeletonPrivate *priv = g_paste_ui_item_skeleton_get_instance_private (self);

    return priv->label;
}

static void
g_paste_ui_item_skeleton_dispose (GObject *object)
{
    GPasteUiItemSkeletonPrivate *priv = g_paste_ui_item_skeleton_get_instance_private (G_PASTE_UI_ITEM_SKELETON (object));

    if (priv->settings)
    {
        g_signal_handler_disconnect (priv->settings, priv->size_id);
        g_clear_object (&priv->settings);
    }

    G_OBJECT_CLASS (g_paste_ui_item_skeleton_parent_class)->dispose (object);
}

static void
g_paste_ui_item_skeleton_class_init (GPasteUiItemSkeletonClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_item_skeleton_dispose;
}

static void
g_paste_ui_item_skeleton_init (GPasteUiItemSkeleton *self)
{
    GPasteUiItemSkeletonPrivate *priv = g_paste_ui_item_skeleton_get_instance_private (self);

    GtkWidget *index_label = gtk_label_new ("");
    GtkWidget *label = gtk_label_new ("");
    priv->index_label = GTK_LABEL (index_label);
    priv->label = GTK_LABEL (label);

    gtk_widget_set_margin_start (index_label, 5);
    gtk_widget_set_margin_end (index_label, 5);
    gtk_widget_set_margin_top (index_label, 5);
    gtk_widget_set_margin_bottom (index_label, 5);
    gtk_widget_set_sensitive (index_label, FALSE);
    gtk_label_set_xalign (priv->index_label, 1.0);
    gtk_label_set_width_chars (priv->index_label, 3);
    gtk_label_set_max_width_chars (priv->index_label, 3);
    gtk_label_set_selectable (priv->index_label, FALSE);
    gtk_label_set_ellipsize (priv->label, PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign (priv->label, 0.0);

    GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_margin_start (hbox, 5);
    gtk_widget_set_margin_end (hbox, 5);
    gtk_box_pack_start (GTK_BOX (hbox), index_label, FALSE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);

    gtk_container_add (GTK_CONTAINER (self), hbox);
}

/**
 * g_paste_ui_item_skeleton_new:
 * @type: the type of the subclass
 * @settings: a #GPasteSettings instance
 *
 * Create a new instance of #GPasteUiItemSkeleton
 *
 * Returns: a newly allocated #GPasteUiItemSkeleton
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_item_skeleton_new (GType           type,
                              GPasteSettings *settings)
{
    g_return_val_if_fail (g_type_is_a (type, G_PASTE_TYPE_UI_ITEM_SKELETON), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    GtkWidget *self = gtk_widget_new (type, "selectable", FALSE, NULL);
    GPasteUiItemSkeletonPrivate *priv = g_paste_ui_item_skeleton_get_instance_private (G_PASTE_UI_ITEM_SKELETON (self));

    priv->settings = g_object_ref (settings);

    priv->size_id = g_signal_connect (settings,
                                      "changed::" G_PASTE_ELEMENT_SIZE_SETTING,
                                      G_CALLBACK (g_paste_ui_item_skeleton_set_text_size),
                                      priv);
    g_paste_ui_item_skeleton_set_text_size (settings, NULL, priv);

    return self;
}
