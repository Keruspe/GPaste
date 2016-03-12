/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-applet-quit.h>

struct _GPasteAppletQuit
{
    GtkMenuItem parent_instance;
};

typedef struct
{
    GApplication *app;
} GPasteAppletQuitPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (AppletQuit, applet_quit, GTK_TYPE_MENU_ITEM)

static void
g_paste_applet_quit_activate (GtkMenuItem *menu_item)
{
    const GPasteAppletQuitPrivate *priv = _g_paste_applet_quit_get_instance_private (G_PASTE_APPLET_QUIT (menu_item));

    g_application_quit (priv->app);

    GTK_MENU_ITEM_CLASS (g_paste_applet_quit_parent_class)->activate (menu_item);
}

static void
g_paste_applet_quit_class_init (GPasteAppletQuitClass *klass)
{
    GTK_MENU_ITEM_CLASS (klass)->activate = g_paste_applet_quit_activate;
}

static void
g_paste_applet_quit_init (GPasteAppletQuit *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_applet_quit_new:
 * @app: the #GApplication
 *
 * Create a new instance of #GPasteAppletQuit
 *
 * Returns: a newly allocated #GPasteAppletQuit
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_applet_quit_new (GApplication *app)
{
    g_return_val_if_fail (G_IS_APPLICATION (app), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_APPLET_QUIT,
                                      "label", _("Quit"),
                                      NULL);
    GPasteAppletQuitPrivate *priv = g_paste_applet_quit_get_instance_private (G_PASTE_APPLET_QUIT (self));

    priv->app = app;

    return self;
}
