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

#include <gpaste-applet-about.h>
#include <gpaste-applet-menu.h>
#include <gpaste-applet-quit.h>
#include <gpaste-applet-settings.h>

#include <glib/gi18n-lib.h>

struct _GPasteAppletMenu
{
    GtkMenu parent_instance;
};

G_DEFINE_TYPE (GPasteAppletMenu, g_paste_applet_menu, GTK_TYPE_MENU)

static void
g_paste_applet_menu_class_init (GPasteAppletMenuClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_applet_menu_init (GPasteAppletMenu *self)
{
    gtk_menu_shell_append (GTK_MENU_SHELL (self), g_paste_applet_settings_new ());
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
G_PASTE_VISIBLE GtkWidget *
g_paste_applet_menu_new (GPasteClient *client,
                         GApplication *app)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail ((!app || G_IS_APPLICATION (app)), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_APPLET_MENU, NULL);
    GtkMenuShell *shell = GTK_MENU_SHELL (self);

    gtk_menu_shell_append (shell, g_paste_applet_about_new (client));
    gtk_menu_shell_append (shell, g_paste_applet_quit_new (app));

    return self;
}
