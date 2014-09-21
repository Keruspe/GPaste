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

#include "gpaste-applet-footer-private.h"

#include <glib/gi18n-lib.h>

struct _GPasteAppletFooterPrivate
{
    GtkWidget *empty;
    GtkWidget *settings;
    GtkWidget *about;
    GtkWidget *quit;
    GtkWidget *separator;
    gboolean   empty_added;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteAppletFooter, g_paste_applet_footer, G_TYPE_OBJECT)

/**
 * g_paste_applet_footer_add_to_menu:
 * @self: a #GPasteAppletFooter instance
 * @menu: The #GtkMenu to which the footer will be added
 * @empty: whether the history is empty or not
 *
 * Add the footer to the botton of a menu
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_applet_footer_add_to_menu (GPasteAppletFooter *self,
                                   GtkMenu            *menu,
                                   gboolean            empty)
{
    g_return_if_fail (G_PASTE_IS_APPLET_FOOTER (self));
    g_return_if_fail (GTK_IS_MENU (menu));

    GPasteAppletFooterPrivate *priv = g_paste_applet_footer_get_instance_private (self);
    GtkMenuShell *shell = GTK_MENU_SHELL (menu);

    gtk_menu_shell_append (shell, g_object_ref (priv->separator));
    if (!empty)
        gtk_menu_shell_append (shell, g_object_ref (priv->empty));
    priv->empty_added = !empty;
    gtk_menu_shell_append (shell, g_object_ref (priv->settings));
    gtk_menu_shell_append (shell, g_object_ref (priv->about));
    if (priv->quit)
        gtk_menu_shell_append (shell, g_object_ref (priv->quit));
}

/**
 * g_paste_applet_footer_remove_from_menu:
 * @self: a #GPasteAppletFooter instance
 * @menu: The #GtkMenu from which the footer will be removed
 *
 * Remove the footer from the menu
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_applet_footer_remove_from_menu (GPasteAppletFooter *self,
                                        GtkMenu            *menu)
{
    g_return_if_fail (G_PASTE_IS_APPLET_FOOTER (self));
    g_return_if_fail (GTK_IS_MENU (menu));

    GPasteAppletFooterPrivate *priv = g_paste_applet_footer_get_instance_private (self);
    GtkContainer *c = GTK_CONTAINER (menu);

    gtk_container_remove (c, priv->separator);
    if (priv->empty_added)
        gtk_container_remove (c, priv->empty);
    gtk_container_remove (c, priv->settings);
    gtk_container_remove (c, priv->about);
    if (priv->quit)
        gtk_container_remove (c, priv->quit);
}

static void
g_paste_applet_footer_dispose (GObject *object)
{
    GPasteAppletFooterPrivate *priv = g_paste_applet_footer_get_instance_private ((GPasteAppletFooter *) object);

    g_clear_object (&priv->separator);
    g_clear_object (&priv->empty);
    g_clear_object (&priv->settings);
    g_clear_object (&priv->about);
    g_clear_object (&priv->quit);

    G_OBJECT_CLASS (g_paste_applet_footer_parent_class)->dispose (object);
}

static void
g_paste_applet_footer_class_init (GPasteAppletFooterClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_applet_footer_dispose;
}

static void
g_paste_applet_footer_init (GPasteAppletFooter *self)
{
    GPasteAppletFooterPrivate *priv = g_paste_applet_footer_get_instance_private (self);

    priv->settings = g_paste_applet_settings_new ();
    priv->separator = gtk_separator_menu_item_new ();
    priv->quit = NULL;
}

/**
 * g_paste_applet_footer_new:
 * @client: a #GPasteClient instance
 * @app: (nullable): the #GApplication to quit
 *
 * Create a new instance of #GPasteAppletFooter
 *
 * Returns: a newly allocated #GPasteAppletFooter
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteAppletFooter *
g_paste_applet_footer_new (GPasteClient *client,
                           GApplication *app)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail ((!app || G_IS_APPLICATION (app)), NULL);

    GPasteAppletFooter *self = g_object_new (G_PASTE_TYPE_APPLET_FOOTER, NULL);
    GPasteAppletFooterPrivate *priv = g_paste_applet_footer_get_instance_private (self);

    priv->empty = g_paste_applet_empty_new (client);
    priv->about = g_paste_applet_about_new (client);
    if (app)
        priv->quit = g_paste_applet_quit_new (app);

    return self;
}
