/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-applet-menu.h>
#include <gpaste-applet-status-icon.h>

struct _GPasteAppletStatusIcon
{
    GPasteAppletIcon parent_instance;
};

enum
{
    C_PRESS,

    C_LAST_SIGNAL
};

typedef struct
{
    GtkStatusIcon *icon;
    GtkMenu       *menu;

    guint64        c_signals[C_LAST_SIGNAL];
} GPasteAppletStatusIconPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (AppletStatusIcon, applet_status_icon, G_PASTE_TYPE_APPLET_ICON)

static gboolean
g_paste_applet_status_icon_popup (GtkStatusIcon  *icon,
                                  GdkEventButton *event,
                                  gpointer        user_data)
{
    GPasteAppletStatusIconPrivate *priv = user_data;

    if (gdk_event_triggers_context_menu ((GdkEvent *) event))
    {
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        gtk_menu_popup (priv->menu, NULL, NULL, gtk_status_icon_position_menu, icon, event->button, event->time);
        G_GNUC_END_IGNORE_DEPRECATIONS
    }
    else
        g_paste_applet_icon_activate ();

    return GDK_EVENT_PROPAGATE;
}

static void
g_paste_applet_status_icon_dispose (GObject *object)
{
    const GPasteAppletStatusIconPrivate *priv = _g_paste_applet_status_icon_get_instance_private (G_PASTE_APPLET_STATUS_ICON (object));

    if (priv->icon)
    {
        g_signal_handler_disconnect (priv->icon, priv->c_signals[C_PRESS]);
        g_clear_object (&priv->icon);
    }

    G_OBJECT_CLASS (g_paste_applet_status_icon_parent_class)->dispose (object);
}

static void
g_paste_applet_status_icon_class_init (GPasteAppletStatusIconClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_applet_status_icon_dispose;
}

static void
g_paste_applet_status_icon_init (GPasteAppletStatusIcon *self)
{
    GPasteAppletStatusIconPrivate *priv = g_paste_applet_status_icon_get_instance_private (self);

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    priv->icon = gtk_status_icon_new_from_icon_name ("edit-paste");
    gtk_status_icon_set_tooltip_text (priv->icon, PACKAGE_STRING);
    gtk_status_icon_set_visible (priv->icon, TRUE);
    G_GNUC_END_IGNORE_DEPRECATIONS

    priv->c_signals[C_PRESS] = g_signal_connect (priv->icon,
                                                 "button-press-event",
                                                 G_CALLBACK (g_paste_applet_status_icon_popup),
                                                 priv);
}

/**
 * g_paste_applet_status_icon_new:
 * @client: a #GPasteClient
 * @app: the #GApplication
 *
 * Create a new instance of #GPasteAppletStatusIcon
 *
 * Returns: a newly allocated #GPasteAppletStatusIcon
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteAppletIcon *
g_paste_applet_status_icon_new (GPasteClient *client,
                                GApplication *app)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (!app || G_IS_APPLICATION (app), NULL);

    GPasteAppletIcon *self = g_paste_applet_icon_new (G_PASTE_TYPE_APPLET_STATUS_ICON, client);
    GPasteAppletStatusIconPrivate *priv = g_paste_applet_status_icon_get_instance_private (G_PASTE_APPLET_STATUS_ICON (self));
    GtkWidget *menu = g_paste_applet_menu_new (client, app);

    gtk_widget_show_all (menu);

    priv->menu = GTK_MENU (menu);

    return self;
}
