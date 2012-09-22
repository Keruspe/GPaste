/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2012 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-daemon-private.h"
#include "gpaste-text-item.h"
#include "gdbus-defines.h"

#include <glib.h>
#include <string.h>

#define G_PASTE_DAEMON_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_DAEMON, GPasteDaemonPrivate))

#define DEFAULT_HISTORY "history"

G_DEFINE_TYPE (GPasteDaemon, g_paste_daemon, G_TYPE_OBJECT)

#define G_PASTE_SEND_DBUS_SIGNAL_FULL(sig,data,num,error)           \
    GPasteDaemonPrivate *priv = self->priv;                         \
    g_dbus_connection_emit_signal (priv->connection,                \
                                   NULL, /* destination_bus_name */ \
                                   priv->object_path,               \
                                   G_PASTE_BUS_NAME,                \
                                   sig,                             \
                                   g_variant_new_tuple (data, num), \
                                   error);

#define G_PASTE_SEND_DBUS_SIGNAL(sig)                  G_PASTE_SEND_DBUS_SIGNAL_FULL(sig, NULL, 0, NULL)
#define G_PASTE_SEND_DBUS_SIGNAL_WITH_ERROR(sig)       G_PASTE_SEND_DBUS_SIGNAL_FULL(sig, NULL, 0, error)
#define G_PASTE_SEND_DBUS_SIGNAL_WITH_DATA(sig,data)   G_PASTE_SEND_DBUS_SIGNAL_FULL(sig, &data, 1, NULL)

#define NEW_SIGNAL(name) \
    g_signal_new (name, \
                  G_PASTE_TYPE_DAEMON,           \
                  G_SIGNAL_RUN_LAST,             \
                  0, /* class offset */          \
                  NULL, /* accumulator */        \
                  NULL, /* accumulator data */   \
                  g_cclosure_marshal_VOID__VOID, \
                  G_TYPE_NONE,                   \
                  0);

struct _GPasteDaemonPrivate
{
    GDBusConnection         *connection;
    gchar                   *object_path;
    guint                    id_on_bus;
    GError                  *inner_error;
    GPasteHistory           *history;
    GPasteSettings          *settings;
    GPasteClipboardsManager *clipboards_manager;
    GPasteKeybinder         *keybinder;
    GDBusNodeInfo           *g_paste_daemon_dbus_info;
    GDBusInterfaceVTable     g_paste_daemon_dbus_vtable;
};

enum
{
    NAME_LOST,
    REEXECUTE_SELF,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

enum
{
    C_CHANGED,
    C_NAME_LOST,
    C_REEXECUTE_SELF,
    C_TRACK,

    C_LAST_SIGNAL
};

static gulong c_signals[C_LAST_SIGNAL] = { 0 };

static void
g_paste_daemon_send_dbus_reply (GDBusConnection       *connection,
                                GDBusMethodInvocation *invocation,
                                GVariant              *reply)
{
    GDBusMessage *reply_message = g_dbus_message_new_method_reply (g_dbus_method_invocation_get_message (invocation));

    if (!reply)
        reply = g_variant_new_tuple (NULL, 0);

    g_dbus_message_set_body (reply_message, reply);
    g_dbus_connection_send_message (connection,
                                    reply_message,
                                    G_DBUS_SEND_MESSAGE_FLAGS_NONE,
                                    NULL, /* out serial */
                                    NULL); /* error */
    g_object_unref (reply_message);
}

static void
g_paste_daemon_get_history (GPasteDaemon          *self,
                            GDBusConnection       *connection,
                            GDBusMethodInvocation *invocation)
{
    GPasteDaemonPrivate *priv = self->priv;
    GSList *history = g_paste_history_get_history (priv->history);
    guint length = MIN (g_slist_length (history), g_paste_settings_get_max_displayed_history_size (priv->settings));
    const gchar **displayed_history = g_new (const gchar *, length + 1);

    for (guint i = 0; i < length; ++i, history = g_slist_next (history))
        displayed_history[i] = g_paste_item_get_display_string (history->data);
    displayed_history[length] = NULL;

    GVariant *variant = g_variant_new_strv (displayed_history, -1);

    g_free (displayed_history);
    g_paste_daemon_send_dbus_reply (connection, invocation, g_variant_new_tuple (&variant, 1));
}

static gchar *
g_paste_daemon_get_dbus_string_parameter (GVariant *parameters,
                                          gsize    *length)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    GVariant *variant = g_variant_iter_next_value (&parameters_iter);
    gchar *value = g_variant_dup_string (variant, length);

    g_variant_unref (variant);

    return value;
}

static void
g_paste_daemon_backup_history (GPasteDaemon          *self,
                               GDBusConnection       *connection,
                               GDBusMethodInvocation *invocation,
                               GVariant              *parameters)
{
    gchar *name = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);

    g_return_if_fail (name != NULL);

    GPasteDaemonPrivate *priv = self->priv;
    GPasteSettings *settings = priv->settings;

    gchar *old_name = g_strdup (g_paste_settings_get_history_name (settings));

    g_paste_settings_set_history_name (settings, name);
    g_paste_history_save (priv->history);
    g_paste_settings_set_history_name (settings, old_name);

    g_free (old_name);
    g_free (name);

    g_paste_daemon_send_dbus_reply (connection, invocation, NULL);
}

static void
g_paste_daemon_switch_history (GPasteDaemon          *self,
                               GDBusConnection       *connection,
                               GDBusMethodInvocation *invocation,
                               GVariant              *parameters)
{
    gchar *name = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);

    g_return_if_fail (name != NULL);

    g_paste_history_switch (self->priv->history, name);

    g_free (name);

    g_paste_daemon_send_dbus_reply (connection, invocation, NULL);
}

static void
g_paste_daemon_delete_history (GPasteDaemon          *self,
                               GDBusConnection       *connection,
                               GDBusMethodInvocation *invocation,
                               GVariant              *parameters)
{
    gchar *name = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);

    g_return_if_fail (name != NULL);

    GPasteDaemonPrivate *priv = self->priv;
    GPasteHistory *history = priv->history;

    gchar *old_history = g_strdup (g_paste_settings_get_history_name (priv->settings));
    gboolean delete_current = g_strcmp0 (name, old_history) == 0;

    if (!delete_current)
        g_paste_history_switch (history, name);
    g_paste_history_delete (history);
    g_paste_history_switch (history, delete_current ? DEFAULT_HISTORY : old_history);

    g_free (name);
    g_free (old_history);

    g_paste_daemon_send_dbus_reply (connection, invocation, NULL);
}

static void
g_paste_daemon_list_histories (GDBusConnection       *connection,
                               GDBusMethodInvocation *invocation)
{
    gchar **history_names = g_paste_history_list ();
    GVariant *variant = g_variant_new_strv ((const gchar **) history_names, -1);

    g_strfreev (history_names);

    g_paste_daemon_send_dbus_reply (connection, invocation, g_variant_new_tuple (&variant, 1));
}

static void
g_paste_daemon_add (GPasteDaemon          *self,
                    GDBusConnection       *connection,
                    GDBusMethodInvocation *invocation,
                    GVariant              *parameters)
{
    gsize length;
    gchar *text = g_paste_daemon_get_dbus_string_parameter (parameters, &length);

    g_return_if_fail (text != NULL);

    GPasteDaemonPrivate *priv = self->priv;
    GPasteSettings *settings = priv->settings;
    gchar *stripped = g_strstrip (g_strdup (text));

    if (length >= g_paste_settings_get_min_text_item_size (settings) &&
        length <= g_paste_settings_get_max_text_item_size (settings) &&
        strlen (stripped) != 0)
    {
        GPasteTextItem *item = g_paste_text_item_new (g_paste_settings_get_trim_items (settings) ? stripped : text);
        g_paste_clipboards_manager_select (priv->clipboards_manager, G_PASTE_ITEM (item));
        g_object_unref (item);
    }
    g_free (text);
    g_free (stripped);

    g_paste_daemon_send_dbus_reply (connection, invocation, NULL);
}

static guint32
g_paste_daemon_get_dbus_uint32_parameter (GVariant *parameters)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    GVariant *variant = g_variant_iter_next_value (&parameters_iter);
    guint32 value = g_variant_get_uint32 (variant);

    g_variant_unref (variant);

    return value;
}

static void
g_paste_daemon_get_element (GPasteDaemon          *self,
                            GDBusConnection       *connection,
                            GDBusMethodInvocation *invocation,
                            GVariant              *parameters)
{
    const gchar *value = g_paste_history_get_value (self->priv->history,
                                                    g_paste_daemon_get_dbus_uint32_parameter (parameters));
    GVariant *variant = g_variant_new_string ((value == NULL) ? "" : value);

    g_paste_daemon_send_dbus_reply (connection, invocation, g_variant_new_tuple (&variant, 1));
}

static void
g_paste_daemon_select (GPasteDaemon          *self,
                       GDBusConnection       *connection,
                       GDBusMethodInvocation *invocation,
                       GVariant              *parameters)
{
    g_paste_history_select (self->priv->history,
                            g_paste_daemon_get_dbus_uint32_parameter (parameters));
    g_paste_daemon_send_dbus_reply (connection, invocation, NULL);
}

static void
g_paste_daemon_delete (GPasteDaemon          *self,
                       GDBusConnection       *connection,
                       GDBusMethodInvocation *invocation,
                       GVariant              *parameters)
{
    g_paste_history_remove (self->priv->history,
                            g_paste_daemon_get_dbus_uint32_parameter (parameters));
    g_paste_daemon_send_dbus_reply (connection, invocation, NULL);
}

static void
g_paste_daemon_empty (GPasteDaemon          *self,
                      GDBusConnection       *connection,
                      GDBusMethodInvocation *invocation)
{
    g_paste_history_empty (self->priv->history);
    g_paste_daemon_send_dbus_reply (connection, invocation, NULL);
}

static void
g_paste_daemon_tracking (GPasteDaemon *self,
                         gboolean      tracking_state,
                         gpointer      user_data G_GNUC_UNUSED)
{
    GVariant *variant = g_variant_new_boolean (tracking_state);

    G_PASTE_SEND_DBUS_SIGNAL_WITH_DATA (SIG_TRACKING, variant)
}

static void
g_paste_daemon_changed (GPasteDaemon *self,
                        gpointer      user_data G_GNUC_UNUSED)
{
    G_PASTE_SEND_DBUS_SIGNAL (SIG_CHANGED)
}

static void
g_paste_daemon_name_lost (GPasteDaemon *self,
                          gpointer      user_data G_GNUC_UNUSED)
{
    G_PASTE_SEND_DBUS_SIGNAL (SIG_NAME_LOST)
}

/**
 * g_paste_daemon_show_history:
 * @self: (transfer none): the #GPasteDaemon
 * @error: a #GError
 *
 * Emit the signal to show history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_daemon_show_history (GPasteDaemon *self,
                             GError      **error)
{
    g_return_if_fail (G_PASTE_IS_DAEMON (self));

    G_PASTE_SEND_DBUS_SIGNAL_WITH_ERROR (SIG_SHOW_HISTORY)
}

static void
g_paste_daemon_reexecute_self (GPasteDaemon *self,
                               gpointer      user_data G_GNUC_UNUSED)
{
    G_PASTE_SEND_DBUS_SIGNAL (SIG_REEXECUTE_SELF)
}

static void
g_paste_daemon_track (GPasteDaemon          *self,
                      GDBusConnection       *connection,
                      GDBusMethodInvocation *invocation,
                      GVariant              *parameters)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    GVariant *variant = g_variant_iter_next_value (&parameters_iter);
    gboolean tracking_state = g_variant_get_boolean (variant);

    g_variant_unref (variant);

    g_paste_settings_set_track_changes (self->priv->settings, tracking_state);
    g_paste_daemon_tracking (self, tracking_state, NULL);
    g_paste_daemon_send_dbus_reply (connection, invocation, NULL);
}

static void
g_paste_daemon_on_extension_state_changed (GPasteDaemon          *self,
                                           GDBusConnection       *connection,
                                           GDBusMethodInvocation *invocation,
                                           GVariant              *parameters)
{
    if (g_paste_settings_get_track_extension_state (self->priv->settings))
        g_paste_daemon_track (self, connection, invocation, parameters);
}

static void
g_paste_daemon_reexecute (GPasteDaemon          *self,
                          GDBusConnection       *connection,
                          GDBusMethodInvocation *invocation)
{
    g_paste_daemon_send_dbus_reply (connection, invocation, NULL);
    g_signal_emit (self,
                   signals[REEXECUTE_SELF],
                   0); /* detail */
}

static void
g_paste_daemon_dbus_method_call (GDBusConnection       *connection,
                                 const gchar           *sender G_GNUC_UNUSED,
                                 const gchar           *object_path G_GNUC_UNUSED,
                                 const gchar           *interface_name G_GNUC_UNUSED,
                                 const gchar           *method_name,
                                 GVariant              *parameters,
                                 GDBusMethodInvocation *invocation,
                                 gpointer               user_data)
{
    GPasteDaemon *self = G_PASTE_DAEMON (user_data);

    if (g_strcmp0 (method_name, GET_HISTORY) == 0)
        g_paste_daemon_get_history (self, connection, invocation);
    else if (g_strcmp0 (method_name, BACKUP_HISTORY) == 0)
        g_paste_daemon_backup_history (self, connection, invocation, parameters);
    else if (g_strcmp0 (method_name, SWITCH_HISTORY) == 0)
        g_paste_daemon_switch_history (self, connection, invocation, parameters);
    else if (g_strcmp0 (method_name, DELETE_HISTORY) == 0)
        g_paste_daemon_delete_history (self, connection, invocation, parameters);
    else if (g_strcmp0 (method_name, LIST_HISTORIES) == 0)
        g_paste_daemon_list_histories (connection, invocation);
    else if (g_strcmp0 (method_name, ADD) == 0)
        g_paste_daemon_add (self, connection, invocation, parameters);
    else if (g_strcmp0 (method_name, GET_ELEMENT) == 0)
        g_paste_daemon_get_element (self, connection, invocation, parameters);
    else if (g_strcmp0 (method_name, SELECT) == 0)
        g_paste_daemon_select (self, connection, invocation, parameters);
    else if (g_strcmp0 (method_name, DELETE) == 0)
        g_paste_daemon_delete (self, connection, invocation, parameters);
    else if (g_strcmp0 (method_name, EMPTY) == 0)
        g_paste_daemon_empty (self, connection, invocation);
    else if (g_strcmp0 (method_name, TRACK) == 0)
        g_paste_daemon_track (self, connection, invocation, parameters);
    else if (g_strcmp0 (method_name, ON_EXTENSION_STATE_CHANGED) == 0)
        g_paste_daemon_on_extension_state_changed (self, connection, invocation, parameters);
    else if (g_strcmp0 (method_name, REEXECUTE) == 0)
        g_paste_daemon_reexecute (self, connection, invocation);

    g_object_unref (invocation);
}

static GVariant *
g_paste_daemon_dbus_get_property (GDBusConnection *connection G_GNUC_UNUSED,
                                  const gchar     *sender G_GNUC_UNUSED,
                                  const gchar     *object_path G_GNUC_UNUSED,
                                  const gchar     *interface_name G_GNUC_UNUSED,
                                  const gchar     *property_name,
                                  GError         **error G_GNUC_UNUSED,
                                  gpointer         user_data)
{
    GPasteDaemonPrivate *priv = G_PASTE_DAEMON (user_data)->priv;

    if (g_strcmp0 (property_name, PROP_ACTIVE) == 0)
        return g_variant_new_boolean (g_paste_settings_get_track_changes (priv->settings));

    return NULL;
}

static void
g_paste_daemon_unregister_object (gpointer user_data)
{
    GPasteDaemon *self = G_PASTE_DAEMON (user_data);
    GPasteDaemonPrivate *priv = self->priv;

    g_signal_handler_disconnect (self, c_signals[C_NAME_LOST]);
    g_signal_handler_disconnect (self, c_signals[C_REEXECUTE_SELF]);
    g_signal_handler_disconnect (priv->settings, c_signals[C_TRACK]);
    g_signal_handler_disconnect (priv->history, c_signals[C_CHANGED]);

    g_object_unref (self);
}

static guint
g_paste_daemon_register_object (GPasteDaemon    *self,
                                GDBusConnection *connection,
                                const gchar     *path,
                                GError         **error)
{
    g_return_val_if_fail (G_PASTE_IS_DAEMON (self), 0);

    GPasteDaemonPrivate *priv = self->priv;

    priv->connection = g_object_ref (connection);
    priv->object_path = g_strdup (path);

    guint result = g_dbus_connection_register_object (connection,
                                                      path,
                                                      priv->g_paste_daemon_dbus_info->interfaces[0],
                                                      &priv->g_paste_daemon_dbus_vtable,
                                                      g_object_ref (self),
                                                      g_paste_daemon_unregister_object,
                                                      error);
    if (!result)
        return 0;

    c_signals[C_NAME_LOST] = g_signal_connect (G_OBJECT (self),
                                              "name-lost",
                                               G_CALLBACK (g_paste_daemon_name_lost),
                                               NULL);
    c_signals[C_REEXECUTE_SELF] = g_signal_connect (G_OBJECT (self),
                                                    "reexecute-self",
                                                    G_CALLBACK (g_paste_daemon_reexecute_self),
                                                    NULL);
    c_signals[C_TRACK] = g_signal_connect_swapped (G_OBJECT (priv->settings),
                                                   "track",
                                                   G_CALLBACK (g_paste_daemon_tracking),
                                                   self);
    c_signals[C_CHANGED] = g_signal_connect_swapped (G_OBJECT (priv->history),
                                                     "changed",
                                                     G_CALLBACK (g_paste_daemon_changed),
                                                     self);

    return result;
}

static void
g_paste_daemon_on_bus_acquired (GDBusConnection *connection,
                                const char      *name G_GNUC_UNUSED,
                                gpointer         user_data)
{
    GPasteDaemon *self = G_PASTE_DAEMON (user_data);

    g_paste_daemon_register_object (self,
                                    connection,
                                    G_PASTE_OBJECT_PATH,
                                    &self->priv->inner_error);
}

static void
g_paste_daemon_on_name_lost (GDBusConnection *connection G_GNUC_UNUSED,
                             const char      *name G_GNUC_UNUSED,
                             gpointer         user_data)
{
    g_signal_emit (G_PASTE_DAEMON (user_data),
                   signals[NAME_LOST],
                   0); /* detail */
}


/**
 * g_paste_daemon_own_bus_name:
 * @self: (transfer none): the #GPasteDaemon
 * @error: a #GError
 *
 * Own the bus name
 *
 * Returns:
 */
G_PASTE_VISIBLE gboolean
g_paste_daemon_own_bus_name (GPasteDaemon *self,
                             GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_DAEMON (self), FALSE);

    GPasteDaemonPrivate *priv = self->priv;

    g_return_val_if_fail (!priv->id_on_bus, FALSE);

    priv->inner_error = *error;
    priv->id_on_bus = g_bus_own_name (G_BUS_TYPE_SESSION,
                                      G_PASTE_BUS_NAME,
                                      G_BUS_NAME_OWNER_FLAGS_NONE,
                                      g_paste_daemon_on_bus_acquired,
                                      NULL, /* on_name_acquired */
                                      g_paste_daemon_on_name_lost,
                                      g_object_ref (self),
                                      g_object_unref);

    return (!priv->inner_error);
}


static void
g_paste_daemon_dispose (GObject *object)
{
    GPasteDaemonPrivate *priv = G_PASTE_DAEMON (object)->priv;

    g_bus_unown_name (priv->id_on_bus);
    g_object_unref (priv->connection);
    g_object_unref (priv->history);
    g_object_unref (priv->settings);
    g_object_unref (priv->clipboards_manager);
    g_object_unref (priv->keybinder);
    g_dbus_node_info_unref (priv->g_paste_daemon_dbus_info);

    G_OBJECT_CLASS (g_paste_daemon_parent_class)->dispose (object);
}

static void
g_paste_daemon_finalize (GObject *object)
{
    g_free (G_PASTE_DAEMON (object)->priv->object_path);

    G_OBJECT_CLASS (g_paste_daemon_parent_class)->finalize (object);
}

static void
g_paste_daemon_class_init (GPasteDaemonClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPasteDaemonPrivate));

    G_OBJECT_CLASS (klass)->dispose = g_paste_daemon_dispose;
    G_OBJECT_CLASS (klass)->finalize = g_paste_daemon_finalize;

    signals[NAME_LOST]      = NEW_SIGNAL ("name-lost")
    signals[REEXECUTE_SELF] = NEW_SIGNAL ("reexecute-self")
}

static void
g_paste_daemon_init (GPasteDaemon *self)
{
    GPasteDaemonPrivate *priv = self->priv = G_PASTE_DAEMON_GET_PRIVATE (self);
    GDBusInterfaceVTable *vtable = &priv->g_paste_daemon_dbus_vtable;

    priv->id_on_bus = 0;
    priv->g_paste_daemon_dbus_info = g_dbus_node_info_new_for_xml (
        "<node>"
        "   <interface name='" G_PASTE_INTERFACE_NAME "'>"
        "       <method name='" GET_HISTORY "'>"
        "           <arg type='as' direction='out' />"
        "       </method>"
        "       <method name='" BACKUP_HISTORY "'>"
        "           <arg type='s' direction='in' />"
        "       </method>"
        "       <method name='" SWITCH_HISTORY "'>"
        "           <arg type='s' direction='in' />"
        "       </method>"
        "       <method name='" DELETE_HISTORY "'>"
        "           <arg type='s' direction='in' />"
        "       </method>"
        "       <method name='" LIST_HISTORIES "'>"
        "           <arg type='as' direction='out' />"
        "       </method>"
        "       <method name='" ADD "'>"
        "           <arg type='s' direction='in' />"
        "       </method>"
        "       <method name='" GET_ELEMENT "'>"
        "           <arg type='u' direction='in' />"
        "           <arg type='s' direction='out' />"
        "       </method>"
        "       <method name='" SELECT "'>"
        "           <arg type='u' direction='in' />"
        "       </method>"
        "       <method name='" DELETE "'>"
        "           <arg type='u' direction='in' />"
        "       </method>"
        "       <method name='" EMPTY "' />"
        "       <method name='" TRACK "'>"
        "           <arg type='b' direction='in' />"
        "       </method>"
        "       <method name='" ON_EXTENSION_STATE_CHANGED "'>"
        "           <arg type='b' direction='in' />"
        "       </method>"
        "       <method name='" REEXECUTE "' />"
        "       <signal name='" SIG_REEXECUTE_SELF "' />"
        "       <signal name='" SIG_TRACKING "'>"
        "           <arg type='b' direction='out' />"
        "       </signal>"
        "       <signal name='" SIG_CHANGED "' />"
        "       <signal name='" SIG_NAME_LOST "' />"
        "       <signal name='" SIG_SHOW_HISTORY "' />"
        "       <property name='" PROP_ACTIVE "' type='b' access='read' />"
        "   </interface>"
        "</node>",
        NULL); /* Error */

    vtable->method_call = g_paste_daemon_dbus_method_call;
    vtable->get_property = g_paste_daemon_dbus_get_property;
    vtable->set_property = NULL;
}

/**
 * g_paste_daemon_new:
 * @history: (transfer none): a #GPasteHistory
 * @settings: (transfer none): a #GPasteSettings
 * @clipboards_manager: (transfer none): a #GPasteClipboardsManager
 * @keybinder: (transfer none): a #GPasteKeybinder
 *
 * Create a new instance of #GPasteDaemon
 *
 * Returns: a newly allocated #GPasteDaemon
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteDaemon *
g_paste_daemon_new (GPasteHistory           *history,
                    GPasteSettings          *settings,
                    GPasteClipboardsManager *clipboards_manager,
                    GPasteKeybinder         *keybinder)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (history), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (clipboards_manager), NULL);
    g_return_val_if_fail (G_PASTE_IS_KEYBINDER (keybinder), NULL);

    GPasteDaemon *self = g_object_new (G_PASTE_TYPE_DAEMON, NULL);
    GPasteDaemonPrivate *priv = self->priv;

    priv->history = g_object_ref (history);
    priv->settings = g_object_ref (settings);
    priv->clipboards_manager = g_object_ref (clipboards_manager);
    priv->keybinder = g_object_ref (keybinder);

    return self;
}
