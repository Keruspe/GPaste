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

#include <gpaste-ui-delete.h>

struct _GPasteUiDelete
{
    GtkButton parent_instance;
};

typedef struct
{
    GPasteClient *client;

    guint32       index;
} GPasteUiDeletePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiDelete, g_paste_ui_delete, GTK_TYPE_BUTTON)

/**
 * g_paste_ui_delete_set_index:
 * @self: a #GPasteUiDelete instance
 * @index: the index of the corresponding item
 *
 * Track a new index
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_delete_set_index (GPasteUiDelete *self,
                             guint32         index)
{
    g_return_if_fail (G_PASTE_IS_UI_DELETE (self));
    GPasteUiDeletePrivate *priv = g_paste_ui_delete_get_instance_private (self);

    priv->index = index;
}

static gboolean
g_paste_ui_delete_button_press_event (GtkWidget      *widget,
                                      GdkEventButton *event G_GNUC_UNUSED)
{
    GPasteUiDeletePrivate *priv = g_paste_ui_delete_get_instance_private (G_PASTE_UI_DELETE (widget));

    g_paste_client_delete (priv->client, priv->index, NULL, NULL);

    return TRUE;
}

static void
g_paste_ui_delete_dispose (GObject *object)
{
    GPasteUiDeletePrivate *priv = g_paste_ui_delete_get_instance_private (G_PASTE_UI_DELETE (object));

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_delete_parent_class)->dispose (object);
}

static void
g_paste_ui_delete_class_init (GPasteUiDeleteClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_delete_dispose;
    GTK_WIDGET_CLASS (klass)->button_press_event = g_paste_ui_delete_button_press_event;
}

static void
g_paste_ui_delete_init (GPasteUiDelete *self)
{
    GtkWidget *button = GTK_WIDGET (self);
    GtkWidget *icon = gtk_image_new_from_icon_name ("edit-delete-symbolic", GTK_ICON_SIZE_MENU);

    gtk_widget_set_margin_start (button, 5);
    gtk_widget_set_margin_end (button, 5);
    gtk_widget_set_margin_start (icon, 5);
    gtk_widget_set_margin_end (icon, 5);

    gtk_container_add (GTK_CONTAINER (self), icon);
}

/**
 * g_paste_ui_delete_new:
 * @client: a #GPasteClient
 * @index: the index of the corresponding item
 *
 * Create a new instance of #GPasteUiDelete
 *
 * Returns: a newly allocated #GPasteUiDelete
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_delete_new (GPasteClient *client,
                       guint32       index)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_DELETE, NULL);
    GPasteUiDeletePrivate *priv = g_paste_ui_delete_get_instance_private (G_PASTE_UI_DELETE (self));

    priv->client = g_object_ref (client);
    priv->index = index;

    return self;
}
