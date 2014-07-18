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

#include "gpaste-applet-private.h"

/*
    Temporary values for menu settings are stored in an int as such
    0x1 << 3 : Is text mode set?
    0x1 << 2 : What is the value of text mode?
    0x1 << 1 : Is Active set?
    0x1 << 0 : What is the value of Active?
*/
#define SET_ACTIVE(v, s) v = (v & ~0x1) | (0x1 << 1) | (s & 0x1)
#define SET_TEXT_MODE(v, s) v = (v & ~(0x1 << 2)) | (0x1 << 3) | ((s & 0x1) << 2)

struct _GPasteAppletPrivate
{
    GPasteClient        *client;
    GPasteSettings      *settings;

    GPasteAppletMenu    *menu;
    GPasteAppletHistory *history;
    GPasteAppletIcon    *icon; 

    GApplication        *application;
    GtkWidget           *win;

    guint                init_state;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteApplet, g_paste_applet, G_TYPE_OBJECT)

/**
 * g_paste_applet_get_active:
 * @self: a #GPasteApplet instance
 *
 * Gets whether the switch is in its "on" or "off" state.
 *
 * Returns: TRUE if the switch is active, and FALSE otherwise
 */
G_PASTE_VISIBLE gboolean
g_paste_applet_get_active (const GPasteApplet *self)
{
    g_return_val_if_fail (G_PASTE_IS_APPLET (self), FALSE);

    GPasteAppletPrivate *priv = g_paste_applet_get_instance_private ((GPasteApplet *) self);

    if (G_UNLIKELY (!priv->menu)) /* Not yet initialized */
        return (priv->init_state & 0x1);

    return g_paste_applet_menu_get_active (priv->menu);
}

/**
 * g_paste_applet_set_active:
 * @self: a #GPasteApplet instance
 * @active: TRUE if the switch should be active, and FALSE otherwise
 *
 * Changes the state of the switch to the desired one.
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_applet_set_active (GPasteApplet *self,
                           gboolean      active)
{
    g_return_if_fail (G_PASTE_IS_APPLET (self));

    GPasteAppletPrivate *priv = g_paste_applet_get_instance_private (self);

    if (G_UNLIKELY (!priv->menu)) /* Not yet initialized */
    {
        SET_ACTIVE (priv->init_state, active);
        return;
    }

    g_paste_applet_menu_set_active (priv->menu, active);
}

/**
 * g_paste_applet_set_text_mode:
 * @self: a #GPasteApplet instance
 * @value: Whether to enable text mode or not
 *
 * Enable extra codepaths for when the switch and the delete
 * buttons are not visible.
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_applet_set_text_mode (GPasteApplet *self,
                              gboolean      value)
{
    g_return_if_fail (G_PASTE_IS_APPLET (self));

    GPasteAppletPrivate *priv = g_paste_applet_get_instance_private (self);

    if (G_UNLIKELY (!priv->menu)) /* Not yet initialized */
    {
        SET_TEXT_MODE (priv->init_state, value);
        return;
    }

    g_paste_applet_menu_set_text_mode (priv->menu, value);
    g_paste_applet_history_set_text_mode (priv->history, value);
}

static void
g_paste_applet_dispose (GObject *object)
{
    GPasteAppletPrivate *priv = g_paste_applet_get_instance_private ((GPasteApplet *) object);

    g_clear_object (&priv->client);
    g_clear_object (&priv->settings);
    g_clear_object (&priv->history);
    g_clear_object (&priv->icon);

    G_OBJECT_CLASS (g_paste_applet_parent_class)->dispose (object);
}

static void
g_paste_applet_class_init (GPasteAppletClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_applet_dispose;
}

static void
g_paste_applet_init (GPasteApplet *self)
{
    GPasteAppletPrivate *priv = g_paste_applet_get_instance_private (self);

    priv->settings = g_paste_settings_new ();

    priv->menu = NULL;
    priv->init_state = 0;
}

static gboolean
g_paste_applet_new_finish (GPasteAppletPrivate *priv,
                           GAsyncResult        *res)
{
    G_PASTE_CLEANUP_ERROR_FREE GError *error = NULL;
    
    priv->client = g_paste_client_new_finish (res, &error);
    if (error)
    {
        if (priv->win)
            gtk_window_close (GTK_WINDOW (priv->win)); /* will exit the application */
        return FALSE;
    }

    priv->menu = g_paste_applet_menu_new (priv->client, priv->application);
    priv->history = g_paste_applet_history_new (priv->client, priv->settings, priv->menu);

    if ((priv->init_state >> 1) & 0x1)
        g_paste_applet_menu_set_active (priv->menu, priv->init_state & 0x1);
    if ((priv->init_state >> 3) & 0x1)
    {
        gboolean value = (priv->init_state >> 2) & 0x1;
        g_paste_applet_menu_set_text_mode (priv->menu, value);
        g_paste_applet_history_set_text_mode (priv->history, value);
    }

    return TRUE;
}

#if G_PASTE_CONFIG_ENABLE_UNITY
static void
g_paste_applet_app_indicator_client_ready (GObject      *source_object G_GNUC_UNUSED,
                                           GAsyncResult *res,
                                           gpointer      user_data)
{
    GPasteAppletPrivate *priv = user_data;

    if (!g_paste_applet_new_finish (priv, res))
        return;

    priv->icon = g_paste_applet_app_indicator_new (priv->client, GTK_MENU (priv->menu));
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

    priv->icon = g_paste_applet_status_icon_new (priv->client, GTK_MENU (priv->menu));
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

#if G_PASTE_CONFIG_ENABLE_UNITY
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

    GPasteApplet *self = g_paste_applet_new (application);
    GPasteAppletPrivate *priv = g_paste_applet_get_instance_private (self);

    g_paste_client_new (g_paste_applet_app_indicator_client_ready, priv);
    g_paste_applet_set_text_mode (self, TRUE);

    return self;
}
#endif

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

    GPasteApplet *self = g_paste_applet_new (application);
    GPasteAppletPrivate *priv = g_paste_applet_get_instance_private (self);

    g_paste_client_new (g_paste_applet_status_icon_client_ready, priv);

    return self;
}
