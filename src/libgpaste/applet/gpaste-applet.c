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
} GPasteAppletPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (Applet, applet, G_TYPE_OBJECT)

static void
g_paste_applet_dispose (GObject *object)
{
    const GPasteAppletPrivate *priv = _g_paste_applet_get_instance_private (G_PASTE_APPLET (object));

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
        if (priv->application)
            g_application_release (priv->application);
        return FALSE;
    }

    return TRUE;
}

typedef struct
{
    GPasteAppletPrivate *priv;
    GPasteStatusIconFunc func;
} CallbackData;

static void
g_paste_applet_on_client_ready (GObject      *source_object G_GNUC_UNUSED,
                                GAsyncResult *res,
                                gpointer      user_data)
{
    g_autofree CallbackData *data = user_data;
    GPasteAppletPrivate *priv = data->priv;

    if (!g_paste_applet_new_finish (priv, res))
        return;

    priv->icon = data->func (priv->client, priv->application);
}

/**
 * g_paste_applet_new:
 * @application: the #GtkApplication running
 * @status_icon_func: (scope async): the constructor of our #GPasteAppletIcon
 *
 * Create a new instance of #GPasteApplet
 *
 * Returns: a newly allocated #GPasteApplet
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteApplet *
g_paste_applet_new (GtkApplication      *application,
                    GPasteStatusIconFunc status_icon_func)
{
    GPasteApplet *self = g_object_new (G_PASTE_TYPE_APPLET, NULL);
    GPasteAppletPrivate *priv = g_paste_applet_get_instance_private (self);

    if (application)
    {
        priv->application = G_APPLICATION (application);
        g_application_hold (priv->application);
    }
    else
    {
        priv->application = NULL;
    }

    CallbackData *data = g_new (CallbackData, 1);
    data->priv = priv;
    data->func = status_icon_func;

    g_paste_client_new (g_paste_applet_on_client_ready, data);

    return self;
}
