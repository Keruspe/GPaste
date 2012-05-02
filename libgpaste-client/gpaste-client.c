/*
 *      This file is part of GPaste.
 *
 *      Copyright 2012 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-client-private.h"

#include <gio/gio.h>

#define G_PASTE_CLIENT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_CLIENT, GPasteClientPrivate))

#define G_PASTE_BUS_NAME "org.gnome.GPaste"

G_DEFINE_TYPE (GPasteClient, g_paste_client, G_TYPE_OBJECT)

struct _GPasteClientPrivate
{
    GDBusNodeInfo       *g_paste_daemon_dbus_info;
    GDBusInterfaceVTable g_paste_daemon_dbus_vtable;
};

static void
g_paste_client_dispose (GObject *object)
{
    g_dbus_node_info_unref (G_PASTE_CLIENT (object)->priv->g_paste_daemon_dbus_info);

    G_OBJECT_CLASS (g_paste_client_parent_class)->dispose (object);
}

static void
g_paste_client_finalize (GObject *object)
{
    G_OBJECT_CLASS (g_paste_client_parent_class)->finalize (object);
}

static void
g_paste_client_class_init (GPasteClientClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPasteClientPrivate));

    G_OBJECT_CLASS (klass)->dispose = g_paste_client_dispose;
    G_OBJECT_CLASS (klass)->finalize = g_paste_client_finalize;
}

static void
g_paste_client_init (GPasteClient *self)
{
    GPasteClientPrivate *priv = self->priv = G_PASTE_CLIENT_GET_PRIVATE (self);
    GDBusInterfaceVTable *vtable = &priv->g_paste_daemon_dbus_vtable;

    priv->g_paste_daemon_dbus_info = g_dbus_node_info_new_for_xml (
        "<node>"
        "   <interface name='" G_PASTE_BUS_NAME "'>"
        "       <method name='GetHistory'>"
        "           <arg type='as' direction='out' />"
        "       </method>"
        "       <method name='BackupHistory'>"
        "           <arg type='s' direction='in' />"
        "       </method>"
        "       <method name='SwitchHistory'>"
        "           <arg type='s' direction='in' />"
        "       </method>"
        "       <method name='DeleteHistory'>"
        "           <arg type='s' direction='in' />"
        "       </method>"
        "       <method name='ListHistories'>"
        "           <arg type='as' direction='out' />"
        "       </method>"
        "       <method name='Add'>"
        "           <arg type='s' direction='in' />"
        "       </method>"
        "       <method name='GetElement'>"
        "           <arg type='u' direction='in' />"
        "           <arg type='s' direction='out' />"
        "       </method>"
        "       <method name='Select'>"
        "           <arg type='u' direction='in' />"
        "       </method>"
        "       <method name='Delete'>"
        "           <arg type='u' direction='in' />"
        "       </method>"
        "       <method name='Empty' />"
        "       <method name='Track'>"
        "           <arg type='b' direction='in' />"
        "       </method>"
        "       <method name='OnExtensionStateChanged'>"
        "           <arg type='b' direction='in' />"
        "       </method>"
        "       <method name='Reexecute' />"
        "       <signal name='ReexecuteSelf' />"
        "       <signal name='Tracking'>"
        "           <arg type='b' direction='out' />"
        "       </signal>"
        "       <signal name='Changed' />"
        "       <signal name='ShowHistory' />"
        "       <property name='Active' type='b' access='read' />"
        "   </interface>"
        "</node>",
        NULL); /* Error */

    //vtable->method_call = g_paste_client_dbus_method_call;
    //vtable->get_property = g_paste_client_dbus_get_property;
    vtable->set_property = NULL;
}

/**
 * g_paste_client_new:
 *
 * Create a new instance of #GPasteClient
 *
 * Returns: a newly allocated #GPasteClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClient *
g_paste_client_new (void)
{
    return g_object_new (G_PASTE_TYPE_CLIENT, NULL);
}
