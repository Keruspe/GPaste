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

#include "gpaste-applet-app-indicator-private.h"

#include <libappindicator/app-indicator.h>

struct _GPasteAppletAppIndicatorPrivate
{
    AppIndicator *icon;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteAppletAppIndicator, g_paste_applet_app_indicator, G_PASTE_TYPE_APPLET_ICON)

static void
g_paste_applet_app_indicator_popup (GPasteAppletIcon *self,
                                    GdkEvent         *event)
{
    /* FIXME: position func */
    g_paste_applet_icon_popup (G_PASTE_APPLET_ICON (self), event, NULL, NULL);
}

static void
g_paste_applet_app_indicator_dispose (GObject *object)
{
    GPasteAppletAppIndicatorPrivate *priv = g_paste_applet_app_indicator_get_instance_private ((GPasteAppletAppIndicator *) object);

    g_clear_object (&priv->icon);

    G_OBJECT_CLASS (g_paste_applet_app_indicator_parent_class)->dispose (object);
}

static void
g_paste_applet_app_indicator_class_init (GPasteAppletAppIndicatorClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_applet_app_indicator_dispose;
    G_PASTE_APPLET_ICON_CLASS (klass)->popup = g_paste_applet_app_indicator_popup;
}

static void
g_paste_applet_app_indicator_init (GPasteAppletAppIndicator *self)
{
    GPasteAppletAppIndicatorPrivate *priv = g_paste_applet_app_indicator_get_instance_private (self);

    priv->icon = app_indicator_new ("GPaste", "edit-paste", APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    app_indicator_set_title (priv->icon, "GPaste");
    app_indicator_set_status (priv->icon, APP_INDICATOR_STATUS_ACTIVE);
}

/**
 * g_paste_applet_app_indicator_new:
 * @client: a #GPasteClient
 * @menu: the menu linked to the status icon
 *
 * Create a new instance of #GPasteAppletAppIndicator
 *
 * Returns: a newly allocated #GPasteAppletAppIndicator
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteAppletIcon *
g_paste_applet_app_indicator_new (GPasteClient *client,
                                  GtkMenu      *menu)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (GTK_IS_MENU (menu), NULL);

    GPasteAppletIcon *self = g_paste_applet_icon_new (G_PASTE_TYPE_APPLET_APP_INDICATOR, client, menu);
    GPasteAppletAppIndicatorPrivate *priv = g_paste_applet_app_indicator_get_instance_private ((GPasteAppletAppIndicator *) self);

    app_indicator_set_menu (priv->icon, menu);

    return self;
}
