/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-applet-about.h>
#include <gpaste-applet-menu.h>
#include <gpaste-applet-quit.h>
#include <gpaste-applet-ui.h>

struct _GPasteAppletMenu
{
    GtkMenu parent_instance;
};

G_PASTE_DEFINE_TYPE (AppletMenu, applet_menu, GTK_TYPE_MENU)

static void
g_paste_applet_menu_class_init (GPasteAppletMenuClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_applet_menu_init (GPasteAppletMenu *self)
{
    GtkMenuShell *shell = GTK_MENU_SHELL (self);

    gtk_menu_shell_append (shell, g_paste_applet_ui_new ());
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
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail ((!app || G_IS_APPLICATION (app)), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_APPLET_MENU, NULL);
    GtkMenuShell *shell = GTK_MENU_SHELL (self);

    gtk_menu_shell_append (shell, g_paste_applet_about_new (client));
    gtk_menu_shell_append (shell, g_paste_applet_quit_new (app));

    return self;
}
