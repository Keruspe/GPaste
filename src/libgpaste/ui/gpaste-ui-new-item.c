/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-new-item.h>

#include "gpaste-gtk-compat.h"

struct _GPasteUiNewItem
{
    GtkButton parent_instance;
};

typedef struct
{
    GPasteClient *client;

    GtkWindow    *rootwin;
} GPasteUiNewItemPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UiNewItem, ui_new_item, GTK_TYPE_BUTTON)

static void
g_paste_ui_new_item_clicked (GtkButton *self)
{
    const GPasteUiNewItemPrivate *priv = _g_paste_ui_new_item_get_instance_private (G_PASTE_UI_NEW_ITEM (self));
    GtkWidget *dialog = gtk_dialog_new_with_buttons (PACKAGE_STRING, priv->rootwin,
                                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_USE_HEADER_BAR,
                                                     _("New"),   GTK_RESPONSE_OK,
                                                     _("Cancel"), GTK_RESPONSE_CANCEL,
                                                     NULL);
    GtkDialog *d = GTK_DIALOG (dialog);
    GtkWidget *text = gtk_text_view_new ();
    GtkTextView *tv = GTK_TEXT_VIEW (text);
    GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
    GtkScrolledWindow *sw = GTK_SCROLLED_WINDOW (scroll);

    gtk_text_view_set_wrap_mode (tv, GTK_WRAP_WORD);
    gtk_scrolled_window_set_min_content_height (sw, 300);
    gtk_scrolled_window_set_min_content_width (sw, 600);
    gtk_container_add (GTK_CONTAINER (sw), text);
    gtk_widget_set_vexpand (scroll, TRUE);
    gtk_widget_set_valign (scroll, TRUE);
    gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (d)), scroll, TRUE, TRUE);
    gtk_widget_show_all (scroll);

    if (gtk_dialog_run (d) == GTK_RESPONSE_OK)
    {
        const gchar *txt;

        g_object_get (G_OBJECT (gtk_text_view_get_buffer (tv)), "text", &txt, NULL);
        if (txt && *txt)
            g_paste_client_add (priv->client, txt, NULL, NULL);
    }

    gtk_widget_destroy (dialog);
}

static void
g_paste_ui_new_item_dispose (GObject *object)
{
    GPasteUiNewItemPrivate *priv = g_paste_ui_new_item_get_instance_private (G_PASTE_UI_NEW_ITEM (object));

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_new_item_parent_class)->dispose (object);
}

static void
g_paste_ui_new_item_class_init (GPasteUiNewItemClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_new_item_dispose;
    GTK_BUTTON_CLASS (klass)->clicked = g_paste_ui_new_item_clicked;
}

static void
g_paste_ui_new_item_init (GPasteUiNewItem *self)
{
    GtkWidget *widget = GTK_WIDGET (self);

    gtk_widget_set_tooltip_text (widget, _("New"));
    gtk_widget_set_valign (widget, GTK_ALIGN_CENTER);
    gtk_container_add (GTK_CONTAINER (self), gtk_image_new_from_icon_name ("document-new-symbolic", GTK_ICON_SIZE_BUTTON));
}

/**
 * g_paste_ui_new_item_new:
 * @rootwin: the root #GtkWindow
 * @client: a #GPasteClient
 *
 * Create a new instance of #GPasteUiNewItem
 *
 * Returns: a newly allocated #GPasteUiNewItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_new_item_new (GtkWindow    *rootwin,
                         GPasteClient *client)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_NEW_ITEM, NULL);
    GPasteUiNewItemPrivate *priv = g_paste_ui_new_item_get_instance_private (G_PASTE_UI_NEW_ITEM (self));

    priv->client = g_object_ref (client);
    priv->rootwin = rootwin;

    return self;
}
