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

#include "gpaste-applet-header-private.h"

#include <glib/gi18n-lib.h>

struct _GPasteAppletHeaderPrivate
{
    GtkWidget *sw;
    GtkWidget *separator;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteAppletHeader, g_paste_applet_header, G_TYPE_OBJECT)

/**
 * g_paste_applet_header_get_active:
 * @self: a #GPasteAppletHeader instance
 *
 * Gets whether the switch is in its "on" or "off" state.
 *
 * Returns: TRUE if the switch is active, and FALSE otherwise
 */
G_PASTE_VISIBLE gboolean
g_paste_applet_header_get_active (const GPasteAppletHeader *self)
{
    g_return_val_if_fail (G_PASTE_IS_APPLET_HEADER (self), FALSE);

    GPasteAppletHeaderPrivate *priv = g_paste_applet_header_get_instance_private ((GPasteAppletHeader *) self);
    return g_paste_applet_switch_get_active (G_PASTE_APPLET_SWITCH (priv->sw));
}

/**
 * g_paste_applet_header_set_active:
 * @self: a #GPasteAppletHeader instance
 * @active: TRUE if the switch should be active, and FALSE otherwise
 *
 * Changes the state of the switch to the desired one.
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_applet_header_set_active (GPasteAppletHeader *self,
                                  gboolean            active)
{
    g_return_if_fail (G_PASTE_IS_APPLET_HEADER (self));

    GPasteAppletHeaderPrivate *priv = g_paste_applet_header_get_instance_private (self);
    g_paste_applet_switch_set_active (G_PASTE_APPLET_SWITCH (priv->sw), active);
}

/**
 * g_paste_applet_header_set_text_mode:
 * @self: a #GPasteAppletHeader instance
 * @value: Whether to enable text mode or not
 *
 * Enable extra codepaths for when the switch and the delete
 * buttons are not visible.
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_applet_header_set_text_mode (GPasteAppletHeader *self,
                                     gboolean            value)
{
    g_return_if_fail (G_PASTE_IS_APPLET_HEADER (self));

    GPasteAppletHeaderPrivate *priv = g_paste_applet_header_get_instance_private (self);
    g_paste_applet_switch_set_text_mode (G_PASTE_APPLET_SWITCH (priv->sw), value);
}

/**
 * g_paste_applet_header_add_to_menu:
 * @self: a #GPasteAppletHeader instance
 * @menu: The #GtkMenu to which the header will be added
 *
 * Add the header to the top of a menu
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_applet_header_add_to_menu (GPasteAppletHeader *self,
                                   GtkMenu            *menu)
{
    g_return_if_fail (G_PASTE_IS_APPLET_HEADER (self));
    g_return_if_fail (GTK_IS_MENU (menu));

    GPasteAppletHeaderPrivate *priv = g_paste_applet_header_get_instance_private (self);
    GtkMenuShell *shell = GTK_MENU_SHELL (menu);

    gtk_menu_shell_prepend (shell, g_object_ref (priv->separator));
    gtk_menu_shell_prepend (shell, g_object_ref (priv->sw));
}

/**
 * g_paste_applet_header_remove_from_menu:
 * @self: a #GPasteAppletHeader instance
 * @menu: The #GtkMenu from which the header will be removed
 *
 * Remove the header from the menu
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_applet_header_remove_from_menu (GPasteAppletHeader *self,
                                        GtkMenu            *menu)
{
    g_return_if_fail (G_PASTE_IS_APPLET_HEADER (self));
    g_return_if_fail (GTK_IS_MENU (menu));

    GPasteAppletHeaderPrivate *priv = g_paste_applet_header_get_instance_private (self);
    GtkContainer *c = GTK_CONTAINER (menu);

    gtk_container_remove (c, priv->sw);
    gtk_container_remove (c, priv->separator);
}

static void
g_paste_applet_header_dispose (GObject *object)
{
    GPasteAppletHeaderPrivate *priv = g_paste_applet_header_get_instance_private ((GPasteAppletHeader *) object);

    g_clear_object (&priv->separator);
    g_clear_object (&priv->sw);

    G_OBJECT_CLASS (g_paste_applet_header_parent_class)->dispose (object);
}

static void
g_paste_applet_header_class_init (GPasteAppletHeaderClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_applet_header_dispose;
}

static void
g_paste_applet_header_init (GPasteAppletHeader *self)
{
    GPasteAppletHeaderPrivate *priv = g_paste_applet_header_get_instance_private (self);

    priv->separator = gtk_separator_menu_item_new ();
}

/**
 * g_paste_applet_header_new:
 * @client: a #GPasteClient instance
 *
 * Create a new instance of #GPasteAppletHeader
 *
 * Returns: a newly allocated #GPasteAppletHeader
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteAppletHeader *
g_paste_applet_header_new (GPasteClient *client)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);

    GPasteAppletHeader *self = g_object_new (G_PASTE_TYPE_APPLET_HEADER, NULL);
    GPasteAppletHeaderPrivate *priv = g_paste_applet_header_get_instance_private (self);

    priv->sw = g_paste_applet_switch_new (client);

    return self;
}
