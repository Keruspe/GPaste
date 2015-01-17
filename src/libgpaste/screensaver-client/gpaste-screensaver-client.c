/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-screensaver-client-private.h"

#include "gpaste-gdbus-macros.h"

#define G_PASTE_SCREENSAVER_OBJECT_PATH    "/org/gnome/ScreenSaver"
#define G_PASTE_SCREENSAVER_INTERFACE_NAME "org.gnome.ScreenSaver"

#define G_PASTE_SCREENSAVER_SIG_ACTIVE_CHANGED "ActiveChanged"

#define G_PASTE_SCREENSAVER_INTERFACE                                    \
    "<node>"                                                             \
        "<interface  name='" G_PASTE_SCREENSAVER_INTERFACE_NAME "'>"     \
            "<signal name='" G_PASTE_SCREENSAVER_SIG_ACTIVE_CHANGED "'>" \
                "<arg name='new_value' type='b' />"                      \
            "</signal>"                                                  \
        "</interface>"                                                   \
    "</node>"

G_DEFINE_TYPE (GPasteScreensaverClient, g_paste_screensaver_client, G_TYPE_DBUS_PROXY)

enum
{
    ACTIVE_CHANGED,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
g_paste_screensaver_client_g_signal (GDBusProxy  *proxy,
                                     const gchar *sender_name G_GNUC_UNUSED,
                                     const gchar *signal_name,
                                     GVariant    *parameters)
{
    GPasteScreensaverClient *self = G_PASTE_SCREENSAVER_CLIENT (proxy);

    if (!g_strcmp0 (signal_name, G_PASTE_SCREENSAVER_SIG_ACTIVE_CHANGED))
    {
        GVariantIter params_iter;
        g_variant_iter_init (&params_iter, parameters);
        G_PASTE_CLEANUP_VARIANT_UNREF GVariant *value = g_variant_iter_next_value (&params_iter);
        g_signal_emit (self,
                       signals[ACTIVE_CHANGED],
                       0, /* detail */
                       g_variant_get_boolean (value),
                       NULL);
    }
}

static void
g_paste_screensaver_client_class_init (GPasteScreensaverClientClass *klass G_GNUC_UNUSED)
{
    G_DBUS_PROXY_CLASS (klass)->g_signal = g_paste_screensaver_client_g_signal;

    signals[ACTIVE_CHANGED] = g_signal_new ("active-changed",
                                             G_PASTE_TYPE_SCREENSAVER_CLIENT,
                                             G_SIGNAL_RUN_LAST,
                                             0, /* class offset */
                                             NULL, /* accumulator */
                                             NULL, /* accumulator data */
                                             g_cclosure_marshal_generic,
                                             G_TYPE_NONE,
                                             1,
                                             G_TYPE_BOOLEAN);
}

static void
g_paste_screensaver_client_init (GPasteScreensaverClient *self)
{
    GDBusProxy *proxy = G_DBUS_PROXY (self);

    G_PASTE_CLEANUP_NODE_INFO_UNREF GDBusNodeInfo *screensaver_dbus_info = g_dbus_node_info_new_for_xml (G_PASTE_SCREENSAVER_INTERFACE,
                                                                                                         NULL); /* Error */
    g_dbus_proxy_set_interface_info (proxy, screensaver_dbus_info->interfaces[0]);
}

/**
 * g_paste_screensaver_client_new_sync:
 * @error: Return location for error or %NULL.
 *
 * Create a new instance of #GPasteScreensaverClient
 *
 * Returns: a newly allocated #GPasteScreensaverClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteScreensaverClient *
g_paste_screensaver_client_new_sync (GError **error)
{
    CUSTOM_PROXY_NEW (SCREENSAVER_CLIENT, SCREENSAVER);
}

/**
 * g_paste_screensaver_client_new:
 * @callback: Callback function to invoke when the proxy is ready.
 * @user_data: User data to pass to @callback.
 *
 * Create a new instance of #GPasteScreensaverClient
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_screensaver_client_new (GAsyncReadyCallback callback,
                                gpointer            user_data)
{
    CUSTOM_PROXY_NEW_ASYNC (SCREENSAVER_CLIENT, SCREENSAVER);
}

/**
 * g_paste_screensaver_client_new_finsh:
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback function passed to the async ctor.
 * @error: Return location for error or %NULL.
 *
 * Create a new instance of #GPasteScreensaverClient
 *
 * Returns: a newly allocated #GPasteScreensaverClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteScreensaverClient *
g_paste_screensaver_client_new_finish (GAsyncResult *result,
                                       GError      **error)
{
    CUSTOM_PROXY_NEW_FINISH (SCREENSAVER_CLIENT);
}
