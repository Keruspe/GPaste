/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-applet-delete-private.h"

#include <glib/gi18n-lib.h>

struct _GPasteAppletDeletePrivate
{
    GPasteClient *client;
    guint32       index;

    GdkWindow    *event_window;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteAppletDelete, g_paste_applet_delete, GTK_TYPE_BUTTON)

static void
g_paste_applet_delete_realize (GtkWidget *widget)
{
    GTK_WIDGET_CLASS (g_paste_applet_delete_parent_class)->realize (widget);

    GdkWindow *window = gtk_widget_get_parent_window (widget);
    gtk_widget_set_window (widget, window);
    g_object_ref (window);

    GPasteAppletDeletePrivate *priv = g_paste_applet_delete_get_instance_private (G_PASTE_APPLET_DELETE (widget));

    GtkAllocation allocation;
    gtk_widget_get_allocation (widget, &allocation);

    GdkWindowAttr attributes;
    attributes.x = allocation.x;
    attributes.y = allocation.y;
    attributes.width = allocation.width;
    attributes.height = allocation.height;
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.wclass = GDK_INPUT_ONLY;
    attributes.event_mask = gtk_widget_get_events (widget) | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK;

    priv->event_window = gdk_window_new (gtk_widget_get_parent_window (widget),
                                         &attributes, GDK_WA_X | GDK_WA_Y);
    gtk_widget_register_window (widget, priv->event_window);
}

static void
g_paste_applet_delete_unrealize (GtkWidget *widget)
{
    GPasteAppletDeletePrivate *priv = g_paste_applet_delete_get_instance_private (G_PASTE_APPLET_DELETE (widget));

    gtk_widget_unregister_window (widget, priv->event_window);
    gdk_window_destroy (priv->event_window);
    priv->event_window = NULL;

    GTK_WIDGET_CLASS (g_paste_applet_delete_parent_class)->unrealize (widget);
}

static gboolean
g_paste_applet_delete_show_window (gpointer win)
{
    gdk_window_show ((GdkWindow *) win);
    return FALSE;
}

static void
g_paste_applet_delete_map (GtkWidget *widget)
{
    GTK_WIDGET_CLASS (g_paste_applet_delete_parent_class)->map (widget);

    GPasteAppletDeletePrivate *priv = g_paste_applet_delete_get_instance_private (G_PASTE_APPLET_DELETE (widget));
    /* We need to delay that until next glib's loop to override the GtkMenuItem's one */
    g_source_set_name_by_id (g_idle_add (g_paste_applet_delete_show_window, priv->event_window), "[GPaste] delete");
}

static void
g_paste_applet_delete_unmap (GtkWidget *widget)
{
    GPasteAppletDeletePrivate *priv = g_paste_applet_delete_get_instance_private (G_PASTE_APPLET_DELETE (widget));
    gdk_window_hide (priv->event_window);

    GTK_WIDGET_CLASS (g_paste_applet_delete_parent_class)->unmap (widget);
}

static void
g_paste_applet_delete_size_allocate (GtkWidget     *widget,
                                     GtkAllocation *allocation)
{
    GTK_WIDGET_CLASS (g_paste_applet_delete_parent_class)->size_allocate (widget, allocation);

    GPasteAppletDeletePrivate *priv = g_paste_applet_delete_get_instance_private (G_PASTE_APPLET_DELETE (widget));
    if (gtk_widget_get_realized (widget))
    {
        gdk_window_move_resize (priv->event_window,
                allocation->x, allocation->y,
                allocation->width, allocation->height);
    }
}

static gboolean
g_paste_applet_delete_button_press_event (GtkWidget      *widget,
                                          GdkEventButton *event G_GNUC_UNUSED)
{
    GPasteAppletDeletePrivate *priv = g_paste_applet_delete_get_instance_private ((GPasteAppletDelete *) widget);
    g_paste_client_delete (priv->client, priv->index, NULL, NULL);
    return TRUE;
}

static void
g_paste_applet_delete_dispose (GObject *object)
{
    GPasteAppletDeletePrivate *priv = g_paste_applet_delete_get_instance_private ((GPasteAppletDelete *) object);

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_applet_delete_parent_class)->dispose (object);
}

static void
g_paste_applet_delete_class_init (GPasteAppletDeleteClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_applet_delete_dispose;

    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
    widget_class->realize = g_paste_applet_delete_realize;
    widget_class->unrealize = g_paste_applet_delete_unrealize;
    widget_class->map = g_paste_applet_delete_map;
    widget_class->unmap = g_paste_applet_delete_unmap;
    widget_class->size_allocate = g_paste_applet_delete_size_allocate;
    widget_class->button_press_event = g_paste_applet_delete_button_press_event;
}

static void
g_paste_applet_delete_init (GPasteAppletDelete *self)
{
    gtk_container_add (GTK_CONTAINER (self), gtk_image_new_from_icon_name ("edit-delete-symbolic", GTK_ICON_SIZE_MENU));
}

/**
 * g_paste_applet_delete_new:
 * @client: a #GPasteClient
 * @index: the index of the corresponding item
 *
 * Create a new instance of #GPasteAppletDelete
 *
 * Returns: a newly allocated #GPasteAppletDelete
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_applet_delete_new (GPasteClient *client,
                           guint32       index)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_APPLET_DELETE, NULL);
    GPasteAppletDeletePrivate *priv = g_paste_applet_delete_get_instance_private ((GPasteAppletDelete *) self);
    priv->client = g_object_ref (client);
    priv->index = index;
    return self;
}
