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

#include <gpaste-ui-edit.h>

/* FIXME: it would be great to mark inactive when we're not dealing with a text item */

struct _GPasteUiEdit
{
    GtkButton parent_instance;
};

typedef struct
{
    GPasteClient *client;

    GtkWindow    *rootwin;

    guint32       index;
} GPasteUiEditPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiEdit, g_paste_ui_edit, GTK_TYPE_BUTTON)

/**
 * g_paste_ui_edit_set_index:
 * @self: a #GPasteUiEdit instance
 * @index: the index of the corresponding item
 *
 * Track a new index
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_edit_set_index (GPasteUiEdit *self,
                           guint32       index)
{
    g_return_if_fail (G_PASTE_IS_UI_EDIT (self));
    GPasteUiEditPrivate *priv = g_paste_ui_edit_get_instance_private (self);

    priv->index = index;
}

static void
on_item_ready (GObject      *source_object G_GNUC_UNUSED,
               GAsyncResult *res,
               gpointer      user_data)
{
    GPasteUiEditPrivate *priv = user_data;
    g_autofree gchar *old_txt = g_paste_client_get_raw_element_finish (priv->client, res, NULL);

    if (!old_txt)
        return;

    GtkWidget *dialog = gtk_dialog_new_with_buttons ("GPaste", priv->rootwin,
                                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_USE_HEADER_BAR,
                                                     _("Edit"),   GTK_RESPONSE_OK,
                                                     _("Cancel"), GTK_RESPONSE_CANCEL,
                                                     NULL);
    GtkDialog *d = GTK_DIALOG (dialog);
    GtkWidget *text = gtk_text_view_new ();
    GtkTextView *tv = GTK_TEXT_VIEW (text);
    GtkTextBuffer *buf = gtk_text_view_get_buffer (tv);
    GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
    GtkScrolledWindow *sw = GTK_SCROLLED_WINDOW (scroll);

    gtk_text_view_set_wrap_mode (tv, GTK_WRAP_WORD);
    gtk_text_buffer_set_text (buf, old_txt, -1);
    gtk_scrolled_window_set_min_content_height (sw, 300);
    gtk_scrolled_window_set_min_content_width (sw, 600);
    gtk_container_add (GTK_CONTAINER (sw), text);
    gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (d)), scroll, TRUE, TRUE, 0);
    gtk_widget_show_all (scroll);

    if (gtk_dialog_run (d) == GTK_RESPONSE_OK)
    {
        const gchar *txt;

        g_object_get (G_OBJECT (buf), "text", &txt, NULL);
        /* FIXME: contents validation */
        g_paste_client_replace (priv->client, priv->index, txt, NULL, NULL);
    }

    gtk_widget_destroy (dialog);
}

static gboolean
g_paste_ui_edit_button_press_event (GtkWidget      *widget,
                                    GdkEventButton *event G_GNUC_UNUSED)
{
    GPasteUiEditPrivate *priv = g_paste_ui_edit_get_instance_private (G_PASTE_UI_EDIT (widget));

    g_paste_client_get_raw_element (priv->client, priv->index, on_item_ready, priv);

    return TRUE;
}

static void
g_paste_ui_edit_dispose (GObject *object)
{
    GPasteUiEditPrivate *priv = g_paste_ui_edit_get_instance_private (G_PASTE_UI_EDIT (object));

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_edit_parent_class)->dispose (object);
}

static void
g_paste_ui_edit_class_init (GPasteUiEditClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_edit_dispose;
    GTK_WIDGET_CLASS (klass)->button_press_event = g_paste_ui_edit_button_press_event;
}

static void
g_paste_ui_edit_init (GPasteUiEdit *self)
{
    GtkWidget *icon = gtk_image_new_from_icon_name ("insert-text-symbolic", GTK_ICON_SIZE_MENU);

    gtk_widget_set_tooltip_text (GTK_WIDGET (self), _("Edit"));
    gtk_widget_set_margin_start (icon, 5);
    gtk_widget_set_margin_end (icon, 5);

    gtk_container_add (GTK_CONTAINER (self), icon);
}

/**
 * g_paste_ui_edit_new:
 * @client: a #GPasteClient
 * @rootwin: the root #GtkWindow
 * @index: the index of the corresponding item
 *
 * Create a new instance of #GPasteUiEdit
 *
 * Returns: a newly allocated #GPasteUiEdit
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_edit_new (GPasteClient *client,
                     GtkWindow    *rootwin,
                     guint32       index)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_EDIT, NULL);
    GPasteUiEditPrivate *priv = g_paste_ui_edit_get_instance_private (G_PASTE_UI_EDIT (self));

    priv->client = g_object_ref (client);
    priv->rootwin = rootwin;
    priv->index = index;

    return self;
}
