// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-gsettings-keys.h>

#include <gpaste-ui-edit-item.h>
#include <gpaste-ui-item-action.h>
#include <gpaste-ui-item-skeleton.h>

typedef struct
{
    GPasteSettings *settings;
    GSignalGroup   *settings_signals;

    GSList         *actions;
    GtkWidget      *edit;
    GtkWidget      *upload;

    GtkWidget      *hbox;

    GtkLabel       *index_label;
    GtkInscription *label;
    GtkPicture     *thumbnail;
    GtkWidget      *tooltip_preview; /* cached hover preview, rebuilt on thumbnail change */
    gboolean        editable;
    gboolean        uploadable;
} GPasteUiItemSkeletonPrivate;

G_PASTE_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (UiItemSkeleton, ui_item_skeleton, GTK_TYPE_BOX)

static void
g_paste_ui_item_skeleton_set_text_size (GPasteSettings *settings,
                                        GParamSpec     *pspec G_GNUC_UNUSED,
                                        gpointer        user_data)
{
    GPasteUiItemSkeletonPrivate *priv = user_data;
    guint64 size = g_paste_settings_get_element_size (settings);

    gtk_inscription_set_min_chars (priv->label, size);
    gtk_inscription_set_nat_chars (priv->label, size);
}

static void
g_paste_ui_item_skeleton_on_images_preview_changed (GPasteSettings *settings,
                                                    GParamSpec     *pspec G_GNUC_UNUSED,
                                                    gpointer        user_data)
{
    GPasteUiItemSkeletonPrivate *priv = user_data;

    if (!priv->thumbnail)
        return;

    gboolean has_image = gtk_picture_get_paintable (priv->thumbnail) != NULL;

    if (has_image)
    {
        gint size = MAX ((gint) g_paste_settings_get_images_preview_size (settings), 10);
        gtk_widget_set_size_request (GTK_WIDGET (priv->thumbnail), size, size);
    }

    gtk_widget_set_visible (GTK_WIDGET (priv->thumbnail), has_image && g_paste_settings_get_images_preview (settings));
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
    g_return_if_fail (G_PASTE_IS_UI_ITEM_SKELETON (self));

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
    g_return_if_fail (G_PASTE_IS_UI_ITEM_SKELETON (self));

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
    g_return_if_fail (G_PASTE_IS_UI_ITEM_SKELETON (self));
    g_return_if_fail (g_utf8_validate (text, -1, NULL));

    const GPasteUiItemSkeletonPrivate *priv = g_paste_ui_item_skeleton_get_instance_private (self);

    gtk_inscription_set_attributes (priv->label, NULL);
    gtk_inscription_set_text (priv->label, text);
}

/**
 * g_paste_ui_item_skeleton_set_text_bold:
 * @self: the #GPasteUiItemSkeleton instance
 * @text: the new text for the label, displayed bold
 *
 * Changes the displayed text, rendered in bold
 */
G_PASTE_VISIBLE void
g_paste_ui_item_skeleton_set_text_bold (GPasteUiItemSkeleton *self,
                                        const gchar          *text)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM_SKELETON (self));
    g_return_if_fail (g_utf8_validate (text, -1, NULL));

    const GPasteUiItemSkeletonPrivate *priv = g_paste_ui_item_skeleton_get_instance_private (self);

    g_autoptr (PangoAttrList) attrs = pango_attr_list_new ();
    pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
    gtk_inscription_set_attributes (priv->label, attrs);
    gtk_inscription_set_text (priv->label, text);
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
    g_return_if_fail (G_PASTE_IS_UI_ITEM_SKELETON (self));

    const GPasteUiItemSkeletonPrivate *priv = g_paste_ui_item_skeleton_get_instance_private (self);

    if (index == (guint64) -1 || index == (guint64) -2)
        gtk_label_set_text (priv->index_label, "");
    else
    {
        g_autofree gchar *_index = g_strdup_printf("%" G_GUINT64_FORMAT, index);

        gtk_label_set_text (priv->index_label, _index);
    }

    g_slist_foreach (priv->actions, action_set_uuid, (gpointer) uuid);
}

/**
 * g_paste_ui_item_skeleton_set_thumbnail:
 * @self: a #GPasteUiItemSkeleton
 * @texture: (transfer none) (nullable): a #GdkTexture to use as thumbnail, or %NULL to clear
 *
 * Set the thumbnail for this item if it's an image
 */
G_PASTE_VISIBLE void
g_paste_ui_item_skeleton_set_thumbnail (GPasteUiItemSkeleton *self,
                                        GdkTexture           *texture)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM_SKELETON (self));

    GPasteUiItemSkeletonPrivate *priv = g_paste_ui_item_skeleton_get_instance_private (self);

    gtk_picture_set_paintable (priv->thumbnail, texture ? GDK_PAINTABLE (texture) : NULL);
    /* The cached hover preview is tied to the old paintable; drop it. */
    g_clear_object (&priv->tooltip_preview);
    g_paste_ui_item_skeleton_on_images_preview_changed (priv->settings, NULL, priv);
}

/* Largest dimension, in pixels, of the hover preview. */
#define G_PASTE_UI_ITEM_SKELETON_PREVIEW_SIZE 400

/* Enlarge the small inline thumbnail to a detail preview on hover: the inline
 * picture is deliberately tiny (images-preview-size), so show the full image,
 * capped and aspect-preserved, in a custom tooltip.
 * gtk_widget_set_size_request () only sets a minimum, so it cannot bound the
 * tooltip's natural size: the paintable must be re-rendered with a capped
 * intrinsic size instead. */
static gboolean
g_paste_ui_item_skeleton_on_thumbnail_query_tooltip (GtkWidget  *widget,
                                                     gint        x        G_GNUC_UNUSED,
                                                     gint        y        G_GNUC_UNUSED,
                                                     gboolean    keyboard G_GNUC_UNUSED,
                                                     GtkTooltip *tooltip,
                                                     gpointer    user_data)
{
    GPasteUiItemSkeletonPrivate *priv = user_data;
    GdkPaintable *paintable = gtk_picture_get_paintable (GTK_PICTURE (widget));

    if (!paintable)
        return FALSE;

    /* query-tooltip fires repeatedly while hovering; build the scaled preview
     * once per thumbnail and reuse it (cleared in set_thumbnail/dispose). */
    if (!priv->tooltip_preview)
    {
        gint width = gdk_paintable_get_intrinsic_width (paintable);
        gint height = gdk_paintable_get_intrinsic_height (paintable);
        g_autoptr (GdkPaintable) scaled = NULL;

        /* Re-render the paintable at the capped size: the result's intrinsic
         * size is the one the tooltip lays out with, which set_size_request
         * alone does not achieve here. */
        if (width > 0 && height > 0)
        {
            gdouble scale = MIN (1.0, MIN ((gdouble) G_PASTE_UI_ITEM_SKELETON_PREVIEW_SIZE / width,
                                           (gdouble) G_PASTE_UI_ITEM_SKELETON_PREVIEW_SIZE / height));
            g_autoptr (GtkSnapshot) snapshot = gtk_snapshot_new ();

            gdk_paintable_snapshot (paintable, GDK_SNAPSHOT (snapshot), width * scale, height * scale);
            scaled = gtk_snapshot_to_paintable (snapshot, NULL);
        }

        GtkWidget *preview = gtk_picture_new_for_paintable ((scaled) ? scaled : paintable);

        gtk_picture_set_content_fit (GTK_PICTURE (preview), GTK_CONTENT_FIT_CONTAIN);

        if (!scaled)
            gtk_widget_set_size_request (preview, G_PASTE_UI_ITEM_SKELETON_PREVIEW_SIZE, G_PASTE_UI_ITEM_SKELETON_PREVIEW_SIZE);

        priv->tooltip_preview = g_object_ref_sink (preview);
    }

    gtk_tooltip_set_custom (tooltip, priv->tooltip_preview);

    return TRUE;
}

static void
add_action (gpointer data,
            gpointer user_data)
{
    GtkWidget *w = data;
    GtkBox *b = user_data;

    gtk_widget_set_halign (w, GTK_ALIGN_START);
    gtk_box_append (b, w);
}

static void
delete_item_action (GPasteClient *client,
                    const gchar  *uuid)
{
    g_paste_client_delete (client, uuid, NULL, NULL);
}

static void
upload_item_action (GPasteClient *client,
                    const gchar  *uuid)
{
    g_paste_client_upload (client, uuid, NULL, NULL);
}

static void
g_paste_ui_item_skeleton_dispose (GObject *object)
{
    GPasteUiItemSkeletonPrivate *priv = g_paste_ui_item_skeleton_get_instance_private (G_PASTE_UI_ITEM_SKELETON (object));

    g_clear_object (&priv->settings_signals);
    g_clear_object (&priv->settings);
    g_clear_object (&priv->tooltip_preview);

    g_clear_pointer (&priv->actions, g_slist_free);

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
    GtkWidget *label = gtk_inscription_new (NULL);

    priv->index_label = GTK_LABEL (index_label);
    priv->label = GTK_INSCRIPTION (label);
    priv->editable = TRUE;

    gtk_widget_set_margin_start (index_label, 6);
    gtk_widget_set_margin_end (index_label, 6);
    gtk_widget_set_margin_top (index_label, 6);
    gtk_widget_set_margin_bottom (index_label, 6);
    gtk_widget_set_sensitive (index_label, FALSE);
    gtk_label_set_xalign (priv->index_label, 1.0);
    gtk_label_set_width_chars (priv->index_label, 3);
    gtk_label_set_max_width_chars (priv->index_label, 3);
    gtk_label_set_selectable (priv->index_label, FALSE);
    gtk_inscription_set_text_overflow (priv->label, GTK_INSCRIPTION_OVERFLOW_ELLIPSIZE_END);
    gtk_inscription_set_xalign (priv->label, 0.0);

    /* The skeleton is the row's own box: GtkListView wraps the factory widget in
     * its own GtkListItem, so there is nothing to nest inside here. */
    GtkWidget *hbox = GTK_WIDGET (self);
    priv->hbox = hbox;
    gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_HORIZONTAL);
    gtk_box_set_spacing (GTK_BOX (self), 2);
    gtk_widget_set_margin_start (hbox, 6);
    gtk_widget_set_margin_end (hbox, 6);

    gtk_widget_set_halign (index_label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (hbox), index_label);

    gtk_widget_set_hexpand (label, TRUE);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (hbox), label);

    GtkWidget *thumbnail = gtk_picture_new ();
    priv->thumbnail = GTK_PICTURE (thumbnail);
    gtk_picture_set_content_fit (priv->thumbnail, GTK_CONTENT_FIT_CONTAIN);
    gtk_widget_set_visible (thumbnail, FALSE);
    gtk_widget_set_hexpand (thumbnail, TRUE);
    gtk_widget_set_halign (thumbnail, GTK_ALIGN_FILL);
    gtk_widget_set_has_tooltip (thumbnail, TRUE);
    g_signal_connect (thumbnail, "query-tooltip", G_CALLBACK (g_paste_ui_item_skeleton_on_thumbnail_query_tooltip), priv);

    GtkWidget *thumbnail_container = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand (thumbnail_container, FALSE);
    gtk_widget_set_halign (thumbnail_container, GTK_ALIGN_CENTER);

    gtk_box_append (GTK_BOX (thumbnail_container), thumbnail);
    gtk_box_append (GTK_BOX (hbox), thumbnail_container);
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
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = g_object_new (type, NULL);
    GPasteUiItemSkeletonPrivate *priv = g_paste_ui_item_skeleton_get_instance_private (G_PASTE_UI_ITEM_SKELETON (self));
    GtkWidget *edit = g_paste_ui_edit_item_new (client, rootwin);
    GtkWidget *upload = g_paste_ui_item_action_new_simple (client, "document-send-symbolic", _("Upload"), upload_item_action);
    GtkWidget *delete = g_paste_ui_item_action_new_simple (client, "edit-delete-symbolic", _("Delete"), delete_item_action);

    priv->settings = g_object_ref (settings);
    priv->edit = edit;
    priv->upload = upload;

    priv->actions = g_slist_prepend (priv->actions, edit);
    priv->actions = g_slist_prepend (priv->actions, upload);
    priv->actions = g_slist_prepend (priv->actions, delete);

    /* Reverse so that pack_end order (edit|upload|delete) is preserved with append */
    g_autoptr (GSList) actions_reversed = g_slist_reverse (g_slist_copy (priv->actions));
    g_slist_foreach (actions_reversed, add_action, GTK_BOX (priv->hbox));

    GSignalGroup *settings_signals = priv->settings_signals = g_signal_group_new (G_PASTE_TYPE_SETTINGS);
    g_signal_group_connect (settings_signals,
                            "notify::" G_PASTE_ELEMENT_SIZE_SETTING,
                            G_CALLBACK (g_paste_ui_item_skeleton_set_text_size),
                            priv);
    g_signal_group_connect (settings_signals,
                            "notify::" G_PASTE_IMAGES_PREVIEW_SETTING,
                            G_CALLBACK (g_paste_ui_item_skeleton_on_images_preview_changed),
                            priv);
    g_signal_group_connect (settings_signals,
                            "notify::" G_PASTE_IMAGES_PREVIEW_SIZE_SETTING,
                            G_CALLBACK (g_paste_ui_item_skeleton_on_images_preview_changed),
                            priv);
    g_signal_group_set_target (settings_signals, settings);
    g_paste_ui_item_skeleton_set_text_size (settings, NULL, priv);
    g_paste_ui_item_skeleton_on_images_preview_changed (settings, NULL, priv);

    return self;
}
