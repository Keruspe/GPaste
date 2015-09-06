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

struct _GPasteUiEdit
{
    GtkButton parent_instance;
};

typedef struct
{
    GPasteUiItem *item;
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
                           guint32         index)
{
    g_return_if_fail (G_PASTE_IS_UI_EDIT (self));
    GPasteUiEditPrivate *priv = g_paste_ui_edit_get_instance_private (self);

    priv->index = index;
}

static gboolean
g_paste_ui_edit_button_press_event (GtkWidget      *widget,
                                    GdkEventButton *event G_GNUC_UNUSED)
{
    GPasteUiEditPrivate *priv = g_paste_ui_edit_get_instance_private (G_PASTE_UI_EDIT (widget));

    //TODO g_paste_client_edit (priv->client, priv->index, contents, NULL, NULL);

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

    gtk_widget_set_margin_start (icon, 5);
    gtk_widget_set_margin_end (icon, 5);

    gtk_container_add (GTK_CONTAINER (self), icon);
}

/**
 * g_paste_ui_edit_new:
 * @item: the #GPasteUiItem we're linked to
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
g_paste_ui_edit_new (GPasteUiItem *item,
                     GPasteClient *client,
                     GtkWindow    *rootwin,
                     guint32       index)
{
    g_return_val_if_fail (G_PASTE_IS_UI_ITEM (item), NULL);
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin));

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_EDIT, NULL);
    GPasteUiEditPrivate *priv = g_paste_ui_edit_get_instance_private (G_PASTE_UI_EDIT (self));

    priv->item = item;
    priv->client = g_object_ref (client);
    priv->rootwin = rootwin;
    priv->index = index;

    return self;
}
