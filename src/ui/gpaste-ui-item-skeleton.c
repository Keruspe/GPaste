/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste/gpaste-gsettings-keys.h>

#include <gpaste-ui-delete-item.h>
#include <gpaste-ui-edit-item.h>
#include <gpaste-ui-item-skeleton.h>
#include <gpaste-ui-upload-item.h>

enum
{
    C_SIZE,
    C_IMAGES_PREVIEW,
    C_IMAGES_PREVIEW_SIZE,

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
    GtkImage       *thumbnail;

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

/* Called when the 'images-preview' setting changes */
static void
g_paste_ui_item_skeleton_on_images_preview_changed (GPasteSettings *settings,
                                                     const gchar    *key G_GNUC_UNUSED,
                                                     gpointer        user_data)
{
    GPasteUiItemSkeletonPrivate *priv = user_data;
    gboolean preview_enabled = g_paste_settings_get_images_preview (settings);
    
    /* If previews are disabled, hide existing thumbnails */
    if (!preview_enabled && priv->thumbnail) {
        gtk_widget_hide (GTK_WIDGET (priv->thumbnail));
    } else if (preview_enabled && priv->thumbnail) {
        /* Show thumbnails that were hidden */
        gtk_widget_show (GTK_WIDGET (priv->thumbnail));
    }
}

/* Called when the 'images-preview-size' setting changes */
static void
g_paste_ui_item_skeleton_on_images_preview_size_changed (GPasteSettings *settings,
                                                          const gchar    *key G_GNUC_UNUSED,
                                                          gpointer        user_data)
{
    GPasteUiItemSkeletonPrivate *priv = user_data;
    guint64 size = g_paste_settings_get_images_preview_size (settings);
    
    /* Update the size constraints for the thumbnail */
    if (priv->thumbnail) {
        GtkWidget *thumbnail_widget = GTK_WIDGET (priv->thumbnail);
        GdkPixbuf *pixbuf;
        
        /* Get the current pixbuf if it exists */
        pixbuf = gtk_image_get_pixbuf (priv->thumbnail);
        
        if (pixbuf) {
            /* Scale the existing pixbuf to the new size while maintaining aspect ratio */
            int orig_width = gdk_pixbuf_get_width (pixbuf);
            int orig_height = gdk_pixbuf_get_height (pixbuf);
            double scale_factor;
            
            /* Calculate scaling to fit within the size while preserving aspect ratio */
            if (orig_width > orig_height) {
                scale_factor = (double)size / orig_width;
            } else {
                scale_factor = (double)size / orig_height;
            }
            
            int new_width = (int)(orig_width * scale_factor);
            int new_height = (int)(orig_height * scale_factor);
            
            /* Ensure we have reasonable dimensions */
            new_width = MAX(new_width, 10);
            new_height = MAX(new_height, 10);
            
            /* Create scaled pixbuf with high quality interpolation */
            GdkPixbuf *scaled;
            
            /* For small thumbnail sizes, use higher quality interpolation */
            GdkInterpType interp_type;
            if (size <= 150) {
                /* Use the highest quality interpolation for small images */
                interp_type = GDK_INTERP_HYPER;
            } else if (size <= 300) {
                /* Use very good quality for medium sized images */
                interp_type = GDK_INTERP_TILES; 
            } else {
                /* For larger sizes, use a slightly faster method */
                interp_type = GDK_INTERP_BILINEAR;
            }
            
            scaled = gdk_pixbuf_scale_simple (pixbuf, 
                                          new_width,
                                          new_height,
                                          interp_type);
            
            /* Set the scaled pixbuf to the image */
            if (scaled) {
                /* Ensure thumbnail is visible */
                gtk_widget_set_visible (GTK_WIDGET (priv->thumbnail), TRUE);
                
                /* Update the widget size before setting the pixbuf */
                gtk_widget_set_size_request (GTK_WIDGET (priv->thumbnail), 
                                           new_width,
                                           new_height);
                
                /* Set the pixbuf after sizing the widget */
                gtk_image_set_from_pixbuf (priv->thumbnail, scaled);
                
                /* Store the new size setting in the widget data */
                g_object_set_data (G_OBJECT (thumbnail_widget), "preview-size", GUINT_TO_POINTER (size));
                
                g_object_unref (scaled);
                
                /* Force a redraw of the parent container */
                GtkWidget *parent = gtk_widget_get_parent (thumbnail_widget);
                if (parent) {
                    gtk_widget_queue_resize (parent);
                }
            }
        }
        
        /* Always update the size setting, even if no current pixbuf */
        g_object_set_data (G_OBJECT (thumbnail_widget), "preview-size", GUINT_TO_POINTER (size));
        
        /* Force a redraw */
        gtk_widget_queue_draw (thumbnail_widget);
    }
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
 * g_paste_ui_item_skeleton_set_thumbnail:
 * @self: a #GPasteUiItemSkeleton
 * @pixbuf: (transfer none): the #GdkPixbuf to set as thumbnail or %NULL
 *
 * Set the thumbnail for this item if it's an image
 */
G_PASTE_VISIBLE void
g_paste_ui_item_skeleton_set_thumbnail (GPasteUiItemSkeleton *self,
                                       GdkPixbuf            *pixbuf)
{
    g_return_if_fail (_G_PASTE_IS_UI_ITEM_SKELETON (self));

    const GPasteUiItemSkeletonPrivate *priv = _g_paste_ui_item_skeleton_get_instance_private (self);

    if (pixbuf) {
        /* Create a thumbnail with the size from settings while preserving aspect ratio */
        gint width = gdk_pixbuf_get_width (pixbuf);
        gint height = gdk_pixbuf_get_height (pixbuf);
        
        /* Get the target size from the widget's stored data */
        gint target_size = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(priv->thumbnail), "preview-size"));
        
        /* Use a default size of 100 if not set */
        if (target_size <= 0) {
            target_size = 100;
            
            /* Store the default size for future use */
            g_object_set_data (G_OBJECT(priv->thumbnail), "preview-size", GUINT_TO_POINTER (target_size));
        }
        
        gdouble scale;

        if (width > height) {
            scale = (gdouble) target_size / width;
        } else {
            scale = (gdouble) target_size / height;
        }

        gint scaled_width = (gint) (width * scale);
        gint scaled_height = (gint) (height * scale);

        /* Create scaled pixbuf with high quality interpolation */
        GdkPixbuf *scaled;
        
        /* For small thumbnail sizes, use higher quality interpolation */
        GdkInterpType interp_type;
        if (target_size <= 150) {
            /* Use the highest quality interpolation for small images */
            interp_type = GDK_INTERP_HYPER;
        } else if (target_size <= 300) {
            /* Use very good quality for medium sized images */
            interp_type = GDK_INTERP_TILES; 
        } else {
            /* For larger sizes, use a slightly faster method since 
             * the difference is less noticeable */
            interp_type = GDK_INTERP_BILINEAR;
        }
        
        scaled = gdk_pixbuf_scale_simple(pixbuf, 
                                      scaled_width, 
                                      scaled_height, 
                                      interp_type);

        /* Ensure thumbnail is visible and properly sized */
        gtk_widget_set_visible (GTK_WIDGET (priv->thumbnail), TRUE);
        gtk_image_set_from_pixbuf (priv->thumbnail, scaled);
            
        /* Make sure the thumbnail is displayed with the right size */
        gtk_widget_set_size_request (GTK_WIDGET (priv->thumbnail), 
                                   scaled_width, 
                                   scaled_height);

        g_object_unref (scaled); /* Free the scaled pixbuf */
    } else {
        gtk_image_clear (priv->thumbnail);
        gtk_widget_set_visible (GTK_WIDGET (priv->thumbnail), FALSE);
    }
}

/**
 * g_paste_ui_item_skeleton_get_label:
 * @self: a #GPasteUiItemSkeleton
 *
 * Get the item's label
 *
 * Returns: (transfer none): the label
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
    gtk_box_pack_end (b, w, FALSE, TRUE, 0);
}

static void
g_paste_ui_item_skeleton_dispose (GObject *object)
{
    GPasteUiItemSkeletonPrivate *priv = g_paste_ui_item_skeleton_get_instance_private (G_PASTE_UI_ITEM_SKELETON (object));

    if (priv->settings)
    {
        /* Disconnect all our signal handlers */
        g_signal_handler_disconnect (priv->settings, priv->c_signals[C_SIZE]);
        g_signal_handler_disconnect (priv->settings, priv->c_signals[C_IMAGES_PREVIEW]);
        g_signal_handler_disconnect (priv->settings, priv->c_signals[C_IMAGES_PREVIEW_SIZE]);
        
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

    /* Create thumbnail image */
    GtkWidget *thumbnail = gtk_image_new ();
    priv->thumbnail = GTK_IMAGE (thumbnail);
    gtk_widget_set_visible (thumbnail, FALSE);
    
    /* Allow the thumbnail to expand horizontally */
    gtk_widget_set_hexpand (thumbnail, TRUE);
    gtk_widget_set_halign (thumbnail, GTK_ALIGN_FILL);
    
    /* We'll set the proper size in g_paste_ui_item_skeleton_new when we have access to settings */

    GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5); /* Increased spacing */
    gtk_widget_set_margin_start (hbox, 5);
    gtk_widget_set_margin_end (hbox, 5);
    
    /* Container properties */
    gtk_widget_set_hexpand (hbox, TRUE);
    
    /* Index label configuration */
    gtk_widget_set_halign (index_label, TRUE);
    gtk_box_pack_start (GTK_BOX (hbox), index_label, FALSE, FALSE, 0);
    
    /* Text label configuration */
    gtk_widget_set_hexpand (label, TRUE);
    gtk_widget_set_halign (label, GTK_ALIGN_START); /* Align text to start */
    gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);
    
    /* Create a container for the thumbnail with improved styling */
    GtkWidget *thumbnail_container = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (thumbnail_container), GTK_SHADOW_NONE);
    gtk_widget_set_margin_end (thumbnail_container, 10); /* Right margin instead of left */
    gtk_widget_set_hexpand (thumbnail_container, FALSE);
    gtk_widget_set_halign (thumbnail_container, GTK_ALIGN_CENTER);
    
    /* Add the thumbnail to its container */
    gtk_container_add (GTK_CONTAINER (thumbnail_container), thumbnail);
    
    /* Place the thumbnail container after the text label but BEFORE the action buttons (which will be added later) */
    gtk_box_pack_start (GTK_BOX (hbox), thumbnail_container, FALSE, FALSE, 0);

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
    
    /* Connecter les signaux pour les paramètres de prévisualisation d'images */
    priv->c_signals[C_IMAGES_PREVIEW] = g_signal_connect (settings,
                                                        "changed::" G_PASTE_IMAGES_PREVIEW_SETTING,
                                                        G_CALLBACK (g_paste_ui_item_skeleton_on_images_preview_changed),
                                                        priv);
    g_paste_ui_item_skeleton_on_images_preview_changed (settings, NULL, priv);
    
    priv->c_signals[C_IMAGES_PREVIEW_SIZE] = g_signal_connect (settings,
                                                             "changed::" G_PASTE_IMAGES_PREVIEW_SIZE_SETTING,
                                                             G_CALLBACK (g_paste_ui_item_skeleton_on_images_preview_size_changed),
                                                             priv);
    
    /* Initialize thumbnail size by storing it for future use */
    if (priv->thumbnail) {
        GtkWidget *thumbnail_widget = GTK_WIDGET (priv->thumbnail);
        guint64 size = g_paste_settings_get_images_preview_size (settings);
        g_object_set_data (G_OBJECT (thumbnail_widget), "preview-size", GUINT_TO_POINTER (size));
    }

    return self;
}
