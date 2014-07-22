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

#include "gpaste-applet-menu-private.h"

#include <glib/gi18n-lib.h>

struct _GPasteAppletMenuPrivate
{
    GPasteAppletHeader *header;
    GPasteAppletFooter *footer;
    GtkWidget          *empty;

    gboolean            header_added;
    gboolean            footer_added;
    gboolean            empty_added;

    gsize               size;

    gboolean            wip;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteAppletMenu, g_paste_applet_menu, GTK_TYPE_MENU)

static void
g_paste_applet_menu_pop_header (GPasteAppletMenu *self)
{
    GPasteAppletMenuPrivate *priv = g_paste_applet_menu_get_instance_private (self);

    if (priv->header_added)
    {
        g_paste_applet_header_remove_from_menu (priv->header, GTK_MENU (self));
        priv->header_added = FALSE;
    }
}

static void
g_paste_applet_menu_pop_footer (GPasteAppletMenu *self)
{
    GPasteAppletMenuPrivate *priv = g_paste_applet_menu_get_instance_private (self);

    if (priv->footer_added)
    {
        g_paste_applet_footer_remove_from_menu (priv->footer, GTK_MENU (self));
        priv->footer_added = FALSE;
    }
}

static void
g_paste_applet_menu_ensure_contents (GPasteAppletMenu *self)
{
    GPasteAppletMenuPrivate *priv = g_paste_applet_menu_get_instance_private (self);

    if (priv->wip)
        return;

    GtkMenu *menu = GTK_MENU (self);

    if (!priv->header_added)
    {
        g_paste_applet_header_add_to_menu (priv->header, menu);
        priv->header_added = TRUE;
    }

    if (!priv->size && !priv->empty_added)
    {
        if (priv->footer_added)
            g_paste_applet_menu_pop_footer (self);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), g_object_ref (priv->empty));
        priv->empty_added = TRUE;
    }

    if (!priv->footer_added)
    {
        g_paste_applet_footer_add_to_menu (priv->footer, menu, !priv->size);
        priv->footer_added = TRUE;
    }
}

static void
g_paste_applet_menu_inc_size (GPasteAppletMenu *self)
{
    GPasteAppletMenuPrivate *priv = g_paste_applet_menu_get_instance_private (self);

    if (++priv->size == 1 && priv->empty_added)
    {
        gtk_container_remove (GTK_CONTAINER (self), priv->empty);
        priv->empty_added = FALSE;
    }
}

/**
 * g_paste_applet_menu_append:
 * @self: a #GPasteAppletMenu instance
 * @items: (element-type GPasteAppletItem): the items to append
 *
 * Append the items to the menu
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_applet_menu_append (GPasteAppletMenu *self,
                                 GSList           *items)
{
    g_return_if_fail (G_PASTE_IS_APPLET_MENU (self));

    GPasteAppletMenuPrivate *priv = g_paste_applet_menu_get_instance_private (self);

    priv->wip = TRUE;
    for (GSList *i = items; i; i = g_slist_next (i))
        gtk_menu_shell_append (GTK_MENU_SHELL (self), GTK_WIDGET (i->data));
    priv->wip = FALSE;
    
    g_paste_applet_menu_ensure_contents (self);
}

/**
 * g_paste_applet_menu_prepend:
 * @self: a #GPasteAppletMenu instance
 * @items: (element-type GPasteAppletItem): the items to prepend
 *
 * Prepend the items to the menu
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_applet_menu_prepend (GPasteAppletMenu *self,
                                  GSList           *items)
{
    g_return_if_fail (G_PASTE_IS_APPLET_MENU (self));

    GPasteAppletMenuPrivate *priv = g_paste_applet_menu_get_instance_private (self);

    priv->wip = TRUE;
    for (GSList *i = items; i; i = g_slist_next (i))
        gtk_menu_shell_prepend (GTK_MENU_SHELL (self), GTK_WIDGET (i->data));
    priv->wip = FALSE;
    
    g_paste_applet_menu_ensure_contents (self);
}

/**
 * g_paste_applet_menu_get_active:
 * @self: a #GPasteAppletMenu instance
 *
 * Gets whether the switch is in its "on" or "off" state.
 *
 * Returns: TRUE if the switch is active, and FALSE otherwise
 */
G_PASTE_VISIBLE gboolean
g_paste_applet_menu_get_active (const GPasteAppletMenu *self)
{
    g_return_val_if_fail (G_PASTE_IS_APPLET_MENU (self), FALSE);

    GPasteAppletMenuPrivate *priv = g_paste_applet_menu_get_instance_private ((GPasteAppletMenu *) self);
    return g_paste_applet_header_get_active (priv->header);
}

/**
 * g_paste_applet_menu_set_active:
 * @self: a #GPasteAppletMenu instance
 * @active: TRUE if the switch should be active, and FALSE otherwise
 *
 * Changes the state of the switch to the desired one.
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_applet_menu_set_active (GPasteAppletMenu *self,
                                gboolean          active)
{
    g_return_if_fail (G_PASTE_IS_APPLET_MENU (self));

    GPasteAppletMenuPrivate *priv = g_paste_applet_menu_get_instance_private (self);
    g_paste_applet_header_set_active (priv->header, active);
}

/**
 * g_paste_applet_menu_set_text_mode:
 * @self: a #GPasteAppletMenu instance
 * @value: Whether to enable text mode or not
 *
 * Enable extra codepaths for when the switch and the delete
 * buttons are not visible.
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_applet_menu_set_text_mode (GPasteAppletMenu *self,
                                   gboolean          value)
{
    g_return_if_fail (G_PASTE_IS_APPLET_MENU (self));

    GPasteAppletMenuPrivate *priv = g_paste_applet_menu_get_instance_private (self);
    g_paste_applet_header_set_text_mode (priv->header, value);
}

static void
g_paste_applet_menu_insert (GtkMenuShell *menu_shell,
                            GtkWidget    *child,
                            gint          position)
{
    GPasteAppletMenu *self = G_PASTE_APPLET_MENU (menu_shell);
    gboolean is_item = G_PASTE_IS_APPLET_ITEM (child);
    g_return_if_fail (!is_item || position == 0 || position == -1);

    if (is_item)
    {
        if (position == -1)
            g_paste_applet_menu_pop_footer (self);
        else
            g_paste_applet_menu_pop_header (self);
        g_paste_applet_menu_inc_size (self);
    }
    GTK_MENU_SHELL_CLASS (g_paste_applet_menu_parent_class)->insert (menu_shell, child, position);
    if (is_item)
        g_paste_applet_menu_ensure_contents (self);
    gtk_widget_show_all (GTK_WIDGET (self));
}

static void
g_paste_applet_menu_remove (GtkContainer     *container,
                            GtkWidget        *widget)
{
    GPasteAppletMenu *self = (GPasteAppletMenu *) container;
    GPasteAppletMenuPrivate *priv = g_paste_applet_menu_get_instance_private (self);

    GTK_CONTAINER_CLASS (g_paste_applet_menu_parent_class)->remove (container, widget);

    if (G_PASTE_IS_APPLET_ITEM (widget))
    {
        --priv->size;
        g_paste_applet_menu_ensure_contents (self);
    }
}

static void
g_paste_applet_menu_dispose (GObject *object)
{
    GPasteAppletMenu *self = (GPasteAppletMenu *) object;
    GPasteAppletMenuPrivate *priv = g_paste_applet_menu_get_instance_private (self);

    g_paste_applet_menu_pop_header (self);
    g_paste_applet_menu_pop_footer (self);
    g_clear_object (&priv->header);
    g_clear_object (&priv->footer);
    g_clear_object (&priv->empty);

    G_OBJECT_CLASS (g_paste_applet_menu_parent_class)->dispose (object);
}

static void
g_paste_applet_menu_class_init (GPasteAppletMenuClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_applet_menu_dispose;
    GTK_CONTAINER_CLASS (klass)->remove = g_paste_applet_menu_remove;
    GTK_MENU_SHELL_CLASS (klass)->insert = g_paste_applet_menu_insert;
}

static void
g_paste_applet_menu_init (GPasteAppletMenu *self)
{
    GPasteAppletMenuPrivate *priv = g_paste_applet_menu_get_instance_private (self);

    priv->empty = gtk_menu_item_new_with_label (_("(Empty)"));
    gtk_widget_set_sensitive (priv->empty, FALSE);

    priv->header_added = FALSE;
    priv->footer_added = FALSE;
    priv->empty_added = FALSE;

    priv->size = 0;

    priv->wip = FALSE;
}

/**
 * g_paste_applet_menu_new:
 * @client: a #GPasteClient instance
 * @app: (nullable): the #GApplication to quit
 *
 * Create a new instance of #GPasteAppletMenu
 *
 * Returns: a newly allocated #GPasteAppletMenu
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteAppletMenu *
g_paste_applet_menu_new (GPasteClient *client,
                         GApplication *app)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail ((!app || G_IS_APPLICATION (app)), NULL);

    GPasteAppletMenu *self = G_PASTE_APPLET_MENU (gtk_widget_new (G_PASTE_TYPE_APPLET_MENU, NULL));
    GPasteAppletMenuPrivate *priv = g_paste_applet_menu_get_instance_private (self);

    priv->header = g_paste_applet_header_new (client);
    priv->footer = g_paste_applet_footer_new (client, app);

    g_paste_applet_menu_ensure_contents (self);

    return self;
}
