/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gsettings-keys.h>
#include <gpaste-ui-delete-item.h>
#include <gpaste-ui-edit-item.h>
#include <gpaste-ui-item-skeleton.h>
#include <gpaste-ui-upload-item.h>

#include "gpaste-gtk-compat.h"

enum
{
    C_SIZE,

    C_LAST_SIGNAL
};

typedef struct
{
    GPasteSettings *settings;

    GSList         *actions;
    GtkWidget      *edit;
    GtkWidget      *upload;

    GtkLabel       *index_label;
    GtkLabel       *label;

    gboolean        editable;
    gboolean        uploadable;

    guint64         c_signals[C_LAST_SIGNAL];
} GPasteUiItemSkeletonPrivate;

G_PASTE_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (UiItemSkeleton, ui_item_skeleton, GTK_TYPE_LIST_BOX_ROW)

static void
g_paste_ui_item_skeleton_set_text_size (GPasteSettings *settings,
                                        const gchar    *key G_GNUC_UNUSED,
                                        gpointer        user_data)
{
    GPasteUiItemSkeletonPrivate *priv = user_data;
    guint64 size = g_paste_settings_get_element_size (settings);

    gtk_label_set_width_chars (priv->label, size);
    gtk_label_set_max_width_chars (priv->label, size);
}

static void
action_set_activatable (gpointer data,
                        gpointer user_data)
{
    GtkWidget *w = data;
    gboolean *a = user_data;

    gtk_widget_set_sensitive (w, *a);
}

/**
 * g_paste_ui_item_skeleton_set_activatable:
 * @self: the #GPasteUiItemSkeleton instance
 * @activatable: whether the item should now be activatable or not
 *
 * Mark the item as being activatable or not
 */
G_PASTE_VISIBLE void
g_paste_ui_item_skeleton_set_activatable (GPasteUiItemSkeleton *self,
                                          gboolean              activatable)
{
    g_return_if_fail (_G_PASTE_IS_UI_ITEM_SKELETON (self));

    const GPasteUiItemSkeletonPrivate *priv = _g_paste_ui_item_skeleton_get_instance_private (self);

    gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (self), activatable);
    gtk_widget_set_sensitive (GTK_WIDGET (priv->label), activatable);

    g_slist_foreach (priv->actions, action_set_activatable, &activatable);

    if (priv->edit)
        gtk_widget_set_sensitive (priv->edit, activatable && priv->editable);
    if (priv->upload)
        gtk_widget_set_sensitive (priv->upload, activatable && priv->uploadable);
}

/**
 * g_paste_ui_item_skeleton_set_editable:
 * @self: the #GPasteUiItemSkeleton instance
 * @editable: whether the item should now be editable or not
 *
 * Mark the item as being editable or not
 */
G_PASTE_VISIBLE void
g_paste_ui_item_skeleton_set_editable (GPasteUiItemSkeleton *self,
                                       gboolean              editable)
{
    g_return_if_fail (_G_PASTE_IS_UI_ITEM_SKELETON (self));

    GPasteUiItemSkeletonPrivate *priv = g_paste_ui_item_skeleton_get_instance_private (self);

    priv->editable = editable;

    gtk_widget_set_sensitive (priv->edit, editable);
}

/**
 * g_paste_ui_item_skeleton_set_uploadable:
 * @self: the #GPasteUiItemSkeleton instance
 * @uploadable: whether the item should now be uploadable or not
 *
 * Mark the item as being uploadable or not
 */
G_PASTE_VISIBLE void
g_paste_ui_item_skeleton_set_uploadable (GPasteUiItemSkeleton *self,
                                         gboolean              uploadable)
{
    g_return_if_fail (_G_PASTE_IS_UI_ITEM_SKELETON (self));

    GPasteUiItemSkeletonPrivate *priv = g_paste_ui_item_skeleton_get_instance_private (self);

    priv->uploadable = uploadable;

    gtk_widget_set_sensitive (priv->upload, uploadable);
}

/**
 * g_paste_ui_item_skeleton_set_text:
 * @self: the #GPasteUiItemSkeleton instance
 * @text: the new text for the label
 *
 * Changes the displayed text
 */
G_PASTE_VISIBLE void
g_paste_ui_item_skeleton_set_text (GPasteUiItemSkeleton *self,
                                   const gchar          *text)
{
    g_return_if_fail (_G_PASTE_IS_UI_ITEM_SKELETON (self));
    g_return_if_fail (g_utf8_validate (text, -1, NULL));

    const GPasteUiItemSkeletonPrivate *priv = _g_paste_ui_item_skeleton_get_instance_private (self);

    gtk_label_set_text (priv->label, text);
}

/**
 * g_paste_ui_item_skeleton_set_markup:
 * @self: the #GPasteUiItemSkeleton instance
 * @markup: the new markup for the label
 *
 * Changes the displayed markup
 */
G_PASTE_VISIBLE void
g_paste_ui_item_skeleton_set_markup (GPasteUiItemSkeleton *self,
                                     const gchar          *markup)
{
    g_return_if_fail (_G_PASTE_IS_UI_ITEM_SKELETON (self));
    g_return_if_fail (g_utf8_validate (markup, -1, NULL));

    const GPasteUiItemSkeletonPrivate *priv = _g_paste_ui_item_skeleton_get_instance_private (self);

    gtk_label_set_markup (priv->label, markup);
}

static void
action_set_uuid (gpointer data,
                 gpointer user_data)
{
    GPasteUiItemAction *a = data;
    const gchar *uuid = user_data;

    g_paste_ui_item_action_set_uuid (a, uuid);
}

/**
 * g_paste_ui_item_skeleton_set_index_and_uuid:
 * @self: the #GPasteUiItemSkeleton instance
 * @index: the index of the new item to display
 * @uuid: the uuid of the new item to display
 *
 * Changes the displayed item
 */
G_PASTE_VISIBLE void
g_paste_ui_item_skeleton_set_index_and_uuid (GPasteUiItemSkeleton *self,
                                             guint64               index,
                                             const gchar          *uuid)
{
    g_return_if_fail (_G_PASTE_IS_UI_ITEM_SKELETON (self));

    const GPasteUiItemSkeletonPrivate *priv = _g_paste_ui_item_skeleton_get_instance_private (self);

    if (index == (guint64) -1 || index == (guint64) -2)
    {
        gtk_label_set_text (priv->index_label, "");
    }
    else
    {
        g_autofree gchar *_index = g_strdup_printf("%" G_GUINT64_FORMAT, index);

        gtk_label_set_text (priv->index_label, _index);
    }

    g_slist_foreach (priv->actions, action_set_uuid, (gpointer) uuid);
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
    g_return_val_if_fail (_G_PASTE_IS_UI_ITEM_SKELETON (self), NULL);

    const GPasteUiItemSkeletonPrivate *priv = _g_paste_ui_item_skeleton_get_instance_private (self);

    return priv->label;
}

static void
add_action (gpointer data,
            gpointer user_data)
{
    GtkWidget *w = data;
    GtkBox *b =user_data;

    gtk_widget_set_halign (w, TRUE);
    gtk_box_pack_end (b, w);
}

static void
g_paste_ui_item_skeleton_dispose (GObject *object)
{
    GPasteUiItemSkeletonPrivate *priv = g_paste_ui_item_skeleton_get_instance_private (G_PASTE_UI_ITEM_SKELETON (object));

    if (priv->settings)
    {
        g_signal_handler_disconnect (priv->settings, priv->c_signals[C_SIZE]);
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
    priv->editable = TRUE;

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
    gtk_widget_set_halign (index_label, TRUE);
    gtk_box_pack_start (GTK_BOX (hbox), index_label, FALSE, TRUE);
    gtk_widget_set_hexpand (label, TRUE);
    gtk_widget_set_halign (label, TRUE);
    gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE);

    gtk_container_add (GTK_CONTAINER (self), hbox);
}

/**
 * g_paste_ui_item_skeleton_new:
 * @type: the type of the subclass
 * @client: a #GPasteClient instance
 * @settings: a #GPasteSettings instance
 * @rootwin: the root #GtkWindow
 *
 * Create a new instance of #GPasteUiItemSkeleton
 *
 * Returns: a newly allocated #GPasteUiItemSkeleton
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_item_skeleton_new (GType           type,
                              GPasteClient   *client,
                              GPasteSettings *settings,
                              GtkWindow      *rootwin)
{
    g_return_val_if_fail (g_type_is_a (type, G_PASTE_TYPE_UI_ITEM_SKELETON), NULL);
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = gtk_widget_new (type, "selectable", FALSE, NULL);
    GPasteUiItemSkeletonPrivate *priv = g_paste_ui_item_skeleton_get_instance_private (G_PASTE_UI_ITEM_SKELETON (self));
    GtkWidget *edit = g_paste_ui_edit_item_new (client, rootwin);
    GtkWidget *upload = g_paste_ui_upload_item_new (client);
    GtkWidget *delete = g_paste_ui_delete_item_new (client);

    priv->settings = g_object_ref (settings);
    priv->edit = edit;
    priv->upload = upload;

    priv->actions = g_slist_prepend (priv->actions, edit);
    priv->actions = g_slist_prepend (priv->actions, upload);
    priv->actions = g_slist_prepend (priv->actions, delete);

    g_slist_foreach (priv->actions, add_action, gtk_bin_get_child (GTK_BIN (self)));

    priv->c_signals[C_SIZE] = g_signal_connect (settings,
                                                "changed::" G_PASTE_ELEMENT_SIZE_SETTING,
                                                G_CALLBACK (g_paste_ui_item_skeleton_set_text_size),
                                                priv);
    g_paste_ui_item_skeleton_set_text_size (settings, NULL, priv);

    return self;
}
