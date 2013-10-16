/*
 *      This file is part of GPaste.
 *
 *      Copyright 2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-gnome-shell-client-private.h"

#include "gpaste-gdbus-macros.h"

#include <gio/gio.h>

#define G_PASTE_GNOME_SHELL_BUS_NAME       "org.gnome.Shell"
#define G_PASTE_GNOME_SHELL_OBJECT_PATH    "/org/gnome/Shell"
#define G_PASTE_GNOME_SHELL_INTERFACE_NAME "org.gnome.Shell"

#define G_PASTE_GNOME_SHELL_EVAL               "Eval"
#define G_PASTE_GNOME_SHELL_FOCUS_SEARCH       "FocusSearch"
#define G_PASTE_GNOME_SHELL_SHOW_OSD           "ShowOSD"
#define G_PASTE_GNOME_SHELL_FOCUS_APP          "FocusApp"
#define G_PASTE_GNOME_SHELL_SHOW_APPLICATIONS  "ShowApplications"
#define G_PASTE_GNOME_SHELL_GRAB_ACCELERATOR   "GrabAccelerator"
#define G_PASTE_GNOME_SHELL_GRAB_ACCELERATORS  "GrabAccelerators"
#define G_PASTE_GNOME_SHELL_UNGRAB_ACCELERATOR "UngrabAccelerator"

#define G_PASTE_GNOME_SHELL_SIG_ACCELERATOR_ACTIVATED "AcceleratorActivated"

#define G_PASTE_GNOME_SHELL_PROP_MODE            "Mode"
#define G_PASTE_GNOME_SHELL_PROP_OVERVIEW_ACTIVE "OverviewActive"
#define G_PASTE_GNOME_SHELL_PROP_SHELL_VERSION   "ShellVersion"

#define G_PASTE_GNOME_SHELL_INTERFACE                                                                      \
    "<node>"                                                                                               \
        "<interface  name='" G_PASTE_GNOME_SHELL_INTERFACE_NAME "'>"                                       \
            "<method name='" G_PASTE_GNOME_SHELL_EVAL "'>"                                                 \
                "<arg type='s' direction='in'  name='script'  />"                                          \
                "<arg type='b' direction='out' name='success' />"                                          \
                "<arg type='s' direction='out' name='result'  />"                                          \
            "</method>"                                                                                    \
            "<method name='" G_PASTE_GNOME_SHELL_FOCUS_SEARCH "' />"                                       \
            "<method name='" G_PASTE_GNOME_SHELL_SHOW_OSD "'>"                                             \
                "<arg type='a{sv}' direction='in' name='params' />"                                        \
            "</method>"                                                                                    \
            "<method name='" G_PASTE_GNOME_SHELL_FOCUS_APP "'>"                                            \
                "<arg type='s' direction='in' name='id' />"                                                \
            "</method>"                                                                                    \
            "<method name='" G_PASTE_GNOME_SHELL_SHOW_APPLICATIONS "' />"                                  \
            "<method name='" G_PASTE_GNOME_SHELL_GRAB_ACCELERATOR "'>"                                     \
                "<arg type='s' direction='in'  name='accelerator' />"                                      \
                "<arg type='u' direction='in'  name='flags'       />"                                      \
                "<arg type='u' direction='out' name='action'      />"                                      \
            "</method>"                                                                                    \
            "<method name='" G_PASTE_GNOME_SHELL_GRAB_ACCELERATORS "'>"                                    \
                "<arg type='a(su)' direction='in'  name='accelerators' />"                                 \
                "<arg type='au'    direction='out' name='actions'      />"                                 \
            "</method>"                                                                                    \
            "<method name='" G_PASTE_GNOME_SHELL_UNGRAB_ACCELERATOR "'>"                                   \
                "<arg type='u' direction='in'  name='action'  />"                                          \
                "<arg type='b' direction='out' name='success' />"                                          \
            "</method>"                                                                                    \
            "<signal name='" G_PASTE_GNOME_SHELL_SIG_ACCELERATOR_ACTIVATED "'>"                            \
                "<arg name='action'    type='u' />"                                                        \
                "<arg name='deviceid'  type='u' />"                                                        \
                "<arg name='timestamp' type='u' />"                                                        \
            "</signal>"                                                                                    \
            "<property name='" G_PASTE_GNOME_SHELL_PROP_MODE "'            type='s' access='read'      />" \
            "<property name='" G_PASTE_GNOME_SHELL_PROP_OVERVIEW_ACTIVE "' type='b' access='readwrite' />" \
            "<property name='" G_PASTE_GNOME_SHELL_PROP_SHELL_VERSION "'   type='s' access='read'      />" \
        "</interface>"                                                                                     \
    "</node>"

struct _GPasteGnomeShellClientPrivate
{
    GDBusProxy    *proxy;
    GDBusNodeInfo *gnome_shell_dbus_info;

    gulong         g_signal;

    GError        *connection_error;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteGnomeShellClient, g_paste_gnome_shell_client, G_TYPE_OBJECT)

enum
{
    ACCELERATOR_ACTIVATED,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

#define DBUS_CALL_ONE_PARAM_RET_BOOL(method, param_type, param_name) \
    DBUS_CALL_ONE_PARAM_RET_BOOL_BASE (GPasteGnomeShellClient, g_paste_gnome_shell_client, G_PASTE_IS_GNOME_SHELL_CLIENT, param_type, param_name, G_PASTE_GNOME_SHELL_##method)

#define DBUS_CALL_ONE_PARAM_NO_RETURN(method, param_type, param_name) \
    DBUS_CALL_ONE_PARAM_NO_RETURN_BASE (GPasteGnomeShellClient, g_paste_gnome_shell_client, G_PASTE_IS_GNOME_SHELL_CLIENT, param_type, param_name, G_PASTE_GNOME_SHELL_##method)

#define DBUS_CALL_NO_PARAM_NO_RETURN(method) \
    DBUS_CALL_NO_PARAM_NO_RETURN_BASE (GPasteGnomeShellClient, g_paste_gnome_shell_client, G_PASTE_IS_GNOME_SHELL_CLIENT, G_PASTE_GNOME_SHELL_##method)

#define DBUS_GET_BOOLEAN_PROPERTY(property) \
    DBUS_GET_BOOLEAN_PROPERTY_BASE (GPasteGnomeShellClient, g_paste_gnome_shell_client, G_PASTE_GNOME_SHELL_PROP_##property)

#define DBUS_GET_STRING_PROPERTY(property) \
    DBUS_GET_STRING_PROPERTY_BASE  (GPasteGnomeShellClient, g_paste_gnome_shell_client, G_PASTE_GNOME_SHELL_PROP_##property)

#define DBUS_SET_BOOLEAN_PROPERTY(property, value) \
    DBUS_SET_BOOLEAN_PROPERTY_BASE(GPasteGnomeShellClient, g_paste_gnome_shell_client, G_PASTE_GNOME_SHELL_INTERFACE_NAME, G_PASTE_GNOME_SHELL_PROP_##property, value)

G_PASTE_VISIBLE void
g_paste_gnome_shell_client_focus_search (GPasteGnomeShellClient *self,
                                         GError               **error)
{
    DBUS_CALL_NO_PARAM_NO_RETURN (FOCUS_SEARCH);
}

G_PASTE_VISIBLE void
g_paste_gnome_shell_client_focus_app (GPasteGnomeShellClient *self,
                                      const gchar            *id,
                                      GError                **error)
{
    DBUS_CALL_ONE_PARAM_NO_RETURN (FOCUS_APP, string, id);
}

G_PASTE_VISIBLE void
g_paste_gnome_shell_client_show_applications (GPasteGnomeShellClient *self,
                                              GError                **error)
{
    DBUS_CALL_NO_PARAM_NO_RETURN (SHOW_APPLICATIONS);
}

G_PASTE_VISIBLE gboolean
g_paste_gnome_shell_client_ungrab_accelerator (GPasteGnomeShellClient *self,
                                               guint32                 action,
                                               GError                **error)
{
    DBUS_CALL_ONE_PARAM_RET_BOOL (UNGRAB_ACCELERATOR, uint32, action);
}

/**
 * g_paste_gnome_shell_client_get_mode:
 * @self: a #GPasteGnomeShellClient instance
 *
 * Get the mode gnome-shell is ran as
 *
 * Returns: the "Mode" property
 */
G_PASTE_VISIBLE const gchar *
g_paste_gnome_shell_client_get_mode (GPasteGnomeShellClient *self)
{
    DBUS_GET_STRING_PROPERTY (MODE);
}

/**
 * g_paste_gnome_shell_client_overview_is_active:
 * @self: a #GPasteGnomeShellClient instance
 *
 * Whether the overview is active or not
 *
 * Returns: the "OverviewActive" property
 */
G_PASTE_VISIBLE gboolean
g_paste_gnome_shell_client_overview_is_active (GPasteGnomeShellClient *self)
{
    DBUS_GET_BOOLEAN_PROPERTY (OVERVIEW_ACTIVE);
}

/**
 * g_paste_gnome_shell_client_get_shell_version:
 * @self: a #GPasteGnomeShellClient instance
 *
 * Get the shell version
 *
 * Returns: the "ShellVersion" property
 */
G_PASTE_VISIBLE const gchar *
g_paste_gnome_shell_client_get_shell_version (GPasteGnomeShellClient *self)
{
    DBUS_GET_STRING_PROPERTY (SHELL_VERSION);
}

/**
 * g_paste_gnome_shell_client_overview_set_active:
 * @self: a #GPasteGnomeShellClient instance
 * @value: the active state
 * @error: a pointer to a GError
 *
 * Set whether the overview is active or not
 *
 * Returns: Whether the "OverviewActive" property has been set or not
 */
G_PASTE_VISIBLE gboolean
g_paste_gnome_shell_client_overview_set_active (GPasteGnomeShellClient *self,
                                                gboolean                value,
                                                GError                **error)
{
    /* FIXME: No such interface 'org.freedesktop.DBus.Properties' on object at path '/org/gnome/Shell' */
    DBUS_SET_BOOLEAN_PROPERTY (OVERVIEW_ACTIVE, value);
}

static void
g_paste_gnome_shell_client_handle_signal (GPasteGnomeShellClient *self,
                                          gchar                  *sender_name G_GNUC_UNUSED,
                                          gchar                  *signal_name,
                                          GVariant               *parameters,
                                          gpointer                user_data G_GNUC_UNUSED)
{
    if (!g_strcmp0 (signal_name, G_PASTE_GNOME_SHELL_SIG_ACCELERATOR_ACTIVATED))
    {
        GVariantIter params_iter;
        g_variant_iter_init (&params_iter, parameters);
        G_PASTE_CLEANUP_VARIANT_UNREF GVariant *action    = g_variant_iter_next_value (&params_iter);
        G_PASTE_CLEANUP_VARIANT_UNREF GVariant *deviceid  = g_variant_iter_next_value (&params_iter);
        G_PASTE_CLEANUP_VARIANT_UNREF GVariant *timestamp = g_variant_iter_next_value (&params_iter);
        g_signal_emit (self,
                       signals[ACCELERATOR_ACTIVATED],
                       0, /* detail */
                       g_variant_get_uint32 (action),
                       g_variant_get_uint32 (deviceid),
                       g_variant_get_uint32 (timestamp),
                       NULL);
    }
}

static void
g_paste_gnome_shell_client_dispose (GObject *object)
{
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (G_PASTE_GNOME_SHELL_CLIENT (object));
    GDBusNodeInfo *gnome_shell_dbus_info = priv->gnome_shell_dbus_info;

    if (gnome_shell_dbus_info)
    {
        GDBusProxy *proxy = priv->proxy;

        if (proxy)
        {
            g_signal_handler_disconnect (proxy, priv->g_signal);
            g_clear_object (&priv->proxy);
        }
        g_dbus_node_info_unref (gnome_shell_dbus_info);
        priv->gnome_shell_dbus_info = NULL;
    }

    G_OBJECT_CLASS (g_paste_gnome_shell_client_parent_class)->dispose (object);
}

static void
g_paste_gnome_shell_client_class_init (GPasteGnomeShellClientClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_gnome_shell_client_dispose;

    signals[ACCELERATOR_ACTIVATED] = g_signal_new ("accelerator-activated",
                                                   G_PASTE_TYPE_GNOME_SHELL_CLIENT,
                                                   G_SIGNAL_RUN_LAST,
                                                   0, /* class offset */
                                                   NULL, /* accumulator */
                                                   NULL, /* accumulator data */
                                                   g_cclosure_marshal_VOID__VOID,
                                                   G_TYPE_NONE,
                                                   3,
                                                   G_TYPE_UINT,
                                                   G_TYPE_UINT,
                                                   G_TYPE_UINT);
}

static void
g_paste_gnome_shell_client_init (GPasteGnomeShellClient *self)
{
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    priv->gnome_shell_dbus_info = g_dbus_node_info_new_for_xml (G_PASTE_GNOME_SHELL_INTERFACE,
                                                                NULL); /* Error */

    priv->connection_error = NULL;
    GDBusProxy *proxy = priv->proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                                     G_DBUS_PROXY_FLAGS_NONE,
                                                                     priv->gnome_shell_dbus_info->interfaces[0],
                                                                     G_PASTE_GNOME_SHELL_BUS_NAME,
                                                                     G_PASTE_GNOME_SHELL_OBJECT_PATH,
                                                                     G_PASTE_GNOME_SHELL_INTERFACE_NAME,
                                                                     NULL, /* cancellable */
                                                                     &priv->connection_error);

    if (proxy)
    {
        priv->g_signal = g_signal_connect_swapped (G_OBJECT (proxy),
                                                   "g-signal",
                                                   G_CALLBACK (g_paste_gnome_shell_client_handle_signal),
                                                   self); /* user_data */
    }
}

/**
 * g_paste_gnome_shell_client_new:
 *
 * Create a new instance of #GPasteGnomeShellClient
 *
 * Returns: a newly allocated #GPasteGnomeShellClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteGnomeShellClient *
g_paste_gnome_shell_client_new (GError **error)
{
    GPasteGnomeShellClient *self = g_object_new (G_PASTE_TYPE_GNOME_SHELL_CLIENT, NULL);
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    if (!priv->proxy)
    {
        if (error)
            *error = priv->connection_error;
        g_object_unref (self);
        return NULL;
    }

    return self;
}
