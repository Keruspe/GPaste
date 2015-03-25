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

#include <gpaste-applet.h>
#include <gpaste-applet-app-indicator.h>
#include <gpaste-applet-status-icon.h>
#include <gpaste-util.h>

struct _GPasteApplet
{
    GObject parent_instance;
};

typedef struct
{
    GPasteClient        *client;

    GPasteAppletIcon    *icon; 

    GApplication        *application;
    GtkWidget           *win;
} GPasteAppletPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteApplet, g_paste_applet, G_TYPE_OBJECT)

static void
g_paste_applet_dispose (GObject *object)
{
    GPasteAppletPrivate *priv = g_paste_applet_get_instance_private (G_PASTE_APPLET (object));

    g_clear_object (&priv->client);
    g_clear_object (&priv->icon);

    G_OBJECT_CLASS (g_paste_applet_parent_class)->dispose (object);
}

static void
g_paste_applet_class_init (GPasteAppletClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_applet_dispose;
}

static void
g_paste_applet_init (GPasteApplet *self G_GNUC_UNUSED)
{
}

static gboolean
g_paste_applet_new_finish (GPasteAppletPrivate *priv,
                           GAsyncResult        *res)
{
    g_autoptr (GError) error = NULL;
    
    priv->client = g_paste_client_new_finish (res, &error);
    if (error)
    {
        if (priv->win)
            gtk_window_close (GTK_WINDOW (priv->win)); /* will exit the application */
        return FALSE;
    }

    return TRUE;
}

#ifdef ENABLE_UNITY
static void
g_paste_applet_app_indicator_client_ready (GObject      *source_object G_GNUC_UNUSED,
                                           GAsyncResult *res,
                                           gpointer      user_data)
{
    GPasteAppletPrivate *priv = user_data;

    if (!g_paste_applet_new_finish (priv, res))
        return;

    priv->icon = g_paste_applet_app_indicator_new (priv->client, priv->application);
}
#endif

static void
g_paste_applet_status_icon_client_ready (GObject      *source_object G_GNUC_UNUSED,
                                         GAsyncResult *res,
                                         gpointer      user_data)
{
    GPasteAppletPrivate *priv = user_data;

    if (!g_paste_applet_new_finish (priv, res))
        return;

    priv->icon = g_paste_applet_status_icon_new (priv->client, priv->application);
}

static GPasteApplet *
g_paste_applet_new (GtkApplication *application)
{
    GPasteApplet *self = g_object_new (G_PASTE_TYPE_APPLET, NULL);
    GPasteAppletPrivate *priv = g_paste_applet_get_instance_private (self);

    if (application)
    {
        priv->application = G_APPLICATION (application);
        priv->win = gtk_application_window_new (application);
        gtk_widget_hide (priv->win);
    }
    else
    {
        priv->application = NULL;
        priv->win = NULL;
    }

    return self;
}

/**
 * g_paste_applet_new_app_indicator:
 * @application: the #GtkApplication running
 *
 * Create a new instance of #GPasteApplet
 *
 * Returns: a newly allocated #GPasteApplet
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteApplet *
g_paste_applet_new_app_indicator (GtkApplication *application)
{
    g_return_val_if_fail (G_IS_APPLICATION (application), NULL);

#ifdef ENABLE_UNITY
    if (g_paste_util_has_unity ())
    {
        GPasteApplet *self = g_paste_applet_new (application);
        GPasteAppletPrivate *priv = g_paste_applet_get_instance_private (self);

        g_paste_client_new (g_paste_applet_app_indicator_client_ready, priv);

        return self;
    }
#endif

    g_critical ("gpaste-app-indicator %s", _("is not installed"));
    return NULL;
}

/**
 * g_paste_applet_new_status_icon:
 * @application: (nullable): the #GtkApplication running
 *
 * Create a new instance of #GPasteApplet
 *
 * Returns: a newly allocated #GPasteApplet
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteApplet *
g_paste_applet_new_status_icon (GtkApplication *application)
{
    g_return_val_if_fail (G_IS_APPLICATION (application), NULL);

    if (g_paste_util_has_applet ())
    {
        GPasteApplet *self = g_paste_applet_new (application);
        GPasteAppletPrivate *priv = g_paste_applet_get_instance_private (self);

        g_paste_client_new (g_paste_applet_status_icon_client_ready, priv);

        return self;
    }

    g_critical ("gpaste-applet %s", _("is not installed"));
    return NULL;
}
