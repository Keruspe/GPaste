/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include <gpaste-applet-app-indicator.h>
#include <gpaste-applet-menu.h>

#include <libappindicator/app-indicator.h>

struct _GPasteAppletAppIndicator
{
    GPasteAppletIcon parent_instance;
};

enum
{
    C_TRACKING,

    C_LAST_SIGNAL
};

typedef struct
{
    GPasteClient *client;

    AppIndicator *icon;

    guint64       c_signals[C_LAST_SIGNAL];
} GPasteAppletAppIndicatorPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (AppletAppIndicator, applet_app_indicator, G_PASTE_TYPE_APPLET_ICON)

static inline void
indicator_set_state (AppIndicator *indicator,
                     gboolean      state)
{
    app_indicator_set_status (indicator, (state) ? APP_INDICATOR_STATUS_ACTIVE : APP_INDICATOR_STATUS_PASSIVE);
}

static void
on_tracking_changed (GPasteClient *client G_GNUC_UNUSED,
                     gboolean      state,
                     gpointer      user_data)
{
    GPasteAppletAppIndicatorPrivate *priv = user_data;

    indicator_set_state (priv->icon, state);
}

static void
g_paste_applet_app_indicator_dispose (GObject *object)
{
    GPasteAppletAppIndicatorPrivate *priv = g_paste_applet_app_indicator_get_instance_private (G_PASTE_APPLET_APP_INDICATOR (object));

    if (priv->c_signals[C_TRACKING])
    {
        g_signal_handler_disconnect (priv->client, priv->c_signals[C_TRACKING]);
        priv->c_signals[C_TRACKING] = 0;
    }

    g_clear_object (&priv->icon);
    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_applet_app_indicator_parent_class)->dispose (object);
}

static void
g_paste_applet_app_indicator_class_init (GPasteAppletAppIndicatorClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_applet_app_indicator_dispose;
}

static void
g_paste_applet_app_indicator_init (GPasteAppletAppIndicator *self)
{
    GPasteAppletAppIndicatorPrivate *priv = g_paste_applet_app_indicator_get_instance_private (self);

    priv->icon = app_indicator_new (PACKAGE_STRING, "edit-paste", APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    app_indicator_set_title (priv->icon, PACKAGE_STRING);
}

/**
 * g_paste_applet_app_indicator_new:
 * @client: a #GPasteClient
 * @app: the #GApplication
 *
 * Create a new instance of #GPasteAppletAppIndicator
 *
 * Returns: a newly allocated #GPasteAppletAppIndicator
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteAppletIcon *
g_paste_applet_app_indicator_new (GPasteClient *client,
                                  GApplication *app)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (!app || G_IS_APPLICATION (app), NULL);

    GPasteAppletIcon *self = g_paste_applet_icon_new (G_PASTE_TYPE_APPLET_APP_INDICATOR, client);
    GPasteAppletAppIndicatorPrivate *priv = g_paste_applet_app_indicator_get_instance_private (G_PASTE_APPLET_APP_INDICATOR (self));
    GtkWidget *menu = g_paste_applet_menu_new (client, app);

    priv->client = g_object_ref (client);

    priv->c_signals[C_TRACKING] = g_signal_connect (G_OBJECT (priv->client),
                                                    "tracking",
                                                    G_CALLBACK (on_tracking_changed),
                                                    priv);

    indicator_set_state (priv->icon, g_paste_client_is_active (client));
    gtk_widget_show_all (menu);
    app_indicator_set_menu (priv->icon, GTK_MENU (menu));

    return self;
}
