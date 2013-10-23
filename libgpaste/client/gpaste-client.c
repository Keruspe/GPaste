/*
 *      This file is part of GPaste.
 *
 *      Copyright 2012-2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-gdbus-macros.h"

#include <gpaste-gdbus-defines.h>

#include <gio/gio.h>

struct _GPasteClientPrivate
{
    GDBusProxy    *proxy;
    GDBusNodeInfo *g_paste_daemon_dbus_info;

    gulong         g_signal;

    GError        *connection_error;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteClient, g_paste_client, G_TYPE_OBJECT)

enum
{
    CHANGED,
    NAME_LOST,
    REEXECUTE_SELF,
    SHOW_HISTORY,
    TRACKING,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* Methods / Async */

#define DBUS_CALL_NO_PARAM_ASYNC(method) \
    DBUS_CALL_NO_PARAM_ASYNC_BASE (GPasteClient, g_paste_client, G_PASTE_IS_CLIENT, G_PASTE_GDBUS_##method)

#define DBUS_CALL_ONE_PARAM_ASYNC(method, param_type, param_name) \
    DBUS_CALL_ONE_PARAM_ASYNC_BASE (GPasteClient, g_paste_client, G_PASTE_IS_CLIENT, param_type, param_name, G_PASTE_GDBUS_##method)

/* Methods / Sync */

#define DBUS_CALL_NO_PARAM_NO_RETURN(method) \
    DBUS_CALL_NO_PARAM_NO_RETURN_BASE (GPasteClient, g_paste_client, G_PASTE_IS_CLIENT, G_PASTE_GDBUS_##method)

#define DBUS_CALL_NO_PARAM_RET_STRV(method) \
    DBUS_CALL_NO_PARAM_RET_STRV_BASE (GPasteClient, g_paste_client, G_PASTE_IS_CLIENT, G_PASTE_GDBUS_##method)

#define DBUS_CALL_ONE_PARAM_NO_RETURN(method, param_type, param_name) \
    DBUS_CALL_ONE_PARAM_NO_RETURN_BASE (GPasteClient, g_paste_client, G_PASTE_IS_CLIENT, param_type, param_name, G_PASTE_GDBUS_##method)

#define DBUS_CALL_ONE_PARAM_RET_STRING(method, param_type, param_name) \
    DBUS_CALL_ONE_PARAM_RET_STRING_BASE (GPasteClient, g_paste_client, G_PASTE_IS_CLIENT, param_type, param_name, G_PASTE_GDBUS_##method)

/* Properties */

#define DBUS_GET_BOOLEAN_PROPERTY(property) \
    DBUS_GET_BOOLEAN_PROPERTY_BASE (GPasteClient, g_paste_client, G_PASTE_GDBUS_PROP_##property)

/* Signals */

#define HANDLE_SIGNAL(sig)                                 \
    if (!g_strcmp0 (signal_name, G_PASTE_GDBUS_SIG_##sig)) \
    {                                                      \
        g_signal_emit (self,                               \
                       signals[sig],                       \
                       0, /* detail */                     \
                       NULL);                              \
    }
#define HANDLE_SIGNAL_WITH_DATA(sig, ans_type, variant_type)                                        \
    if (!g_strcmp0 (signal_name, G_PASTE_GDBUS_SIG_##sig))                                          \
    {                                                                                               \
        GVariantIter params_iter;                                                                   \
        g_variant_iter_init (&params_iter, parameters);                                             \
        G_PASTE_CLEANUP_VARIANT_UNREF GVariant *variant = g_variant_iter_next_value (&params_iter); \
        ans_type answer = g_variant_get_##variant_type (variant);                                   \
        g_signal_emit (self,                                                                        \
                       signals[sig],                                                                \
                       0, /* detail */                                                              \
                       answer,                                                                      \
                       NULL);                                                                       \
    }

#define NEW_SIGNAL(name)                         \
    g_signal_new (name,                          \
                  G_PASTE_TYPE_CLIENT,           \
                  G_SIGNAL_RUN_LAST,             \
                  0, /* class offset */          \
                  NULL, /* accumulator */        \
                  NULL, /* accumulator data */   \
                  g_cclosure_marshal_VOID__VOID, \
                  G_TYPE_NONE,                   \
                  0) /* number of params */
#define NEW_SIGNAL_WITH_DATA(name, type)           \
    g_signal_new (name,                            \
                  G_PASTE_TYPE_CLIENT,             \
                  G_SIGNAL_RUN_LAST,               \
                  0, /* class offset */            \
                  NULL, /* accumulator */          \
                  NULL, /* accumulator data */     \
                  g_cclosure_marshal_VOID__##type, \
                  G_TYPE_NONE,                     \
                  1,                               \
                  G_TYPE_##type)

/**
 * g_paste_client_get_element:
 * @self: a #GPasteClient instance
 * @index: the index of the element we want to get
 * @error: a #GError
 *
 * Get an item from the #GPasteDaemon
 *
 * Returns: a newly allocated string
 */
G_PASTE_VISIBLE gchar *
g_paste_client_get_element (GPasteClient *self,
                            guint32       index,
                            GError      **error)
{
    DBUS_CALL_ONE_PARAM_RET_STRING (GET_ELEMENT, uint32, index);
}

/**
 * g_paste_client_get_history:
 * @self: a #GPasteClient instance
 * @error: a #GError
 *
 * Get the history from the #GPasteDaemon
 *
 * Returns: (transfer full): a newly allocated array of string
 */
G_PASTE_VISIBLE GStrv
g_paste_client_get_history (GPasteClient *self,
                            GError      **error)
{
    DBUS_CALL_NO_PARAM_RET_STRV (GET_HISTORY);
}

/**
 * g_paste_client_add:
 * @self: a #GPasteClient instance
 * @text: the text to add
 * @error: a #GError
 *
 * Add an item to the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_add (GPasteClient *self,
                    const gchar  *text,
                    GError      **error)
{
    DBUS_CALL_ONE_PARAM_NO_RETURN (ADD, string, text);
}

/**
 * g_paste_client_add_file:
 * @self: a #GPasteClient instance
 * @file: the file to add
 * @error: a #GError
 *
 * Add the file contents to the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_add_file (GPasteClient *self,
                         const gchar  *file,
                         GError      **error)
{
    G_PASTE_CLEANUP_FREE gchar *absolute_path = NULL;

    if (!g_path_is_absolute (file))
    {
        G_PASTE_CLEANUP_FREE gchar *current_dir = g_get_current_dir ();
        absolute_path = g_build_filename (current_dir, file, NULL);
    }

    DBUS_CALL_ONE_PARAM_NO_RETURN (ADD_FILE, string, ((absolute_path) ? absolute_path : file));
}

/**
 * g_paste_client_select:
 * @self: a #GPasteClient instance
 * @index: the index of the element we want to select
 * @error: a #GError
 *
 * Select an item from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_select (GPasteClient *self,
                       guint32       index,
                       GError      **error)
{
    DBUS_CALL_ONE_PARAM_NO_RETURN (SELECT, uint32, index);
}

/**
 * g_paste_client_delete:
 * @self: a #GPasteClient instance
 * @index: the index of the element we want to delete
 * @error: a #GError
 *
 * Delete an item from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_delete (GPasteClient *self,
                       guint32       index,
                       GError      **error)
{
    DBUS_CALL_ONE_PARAM_NO_RETURN (DELETE, uint32, index);
}

/**
 * g_paste_client_empty:
 * @self: a #GPasteClient instance
 * @error: a #GError
 *
 * Empty the history from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_empty (GPasteClient *self,
                      GError      **error)
{
    DBUS_CALL_NO_PARAM_NO_RETURN (EMPTY);
}

/**
 * g_paste_client_track:
 * @self: a #GPasteClient instance
 * @state: the new tracking state of the #GPasteDaemon
 * @error: a #GError
 *
 * Change the tracking state of the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_track (GPasteClient *self,
                      gboolean      state,
                      GError      **error)
{
    DBUS_CALL_ONE_PARAM_NO_RETURN (TRACK, boolean, state);
}

/**
 * g_paste_client_on_extension_state_changed:
 * @self: a #GPasteClient instance
 * @state: the new state of the extension
 * @error: a #GError
 *
 * Call this when the extension changes its state
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_on_extension_state_changed (GPasteClient *self,
                                           gboolean      state,
                                           GError      **error)
{
    DBUS_CALL_ONE_PARAM_NO_RETURN (ON_EXTENSION_STATE_CHANGED, boolean, state);
}

/**
 * g_paste_client_reexecute:
 * @self: a #GPasteClient instance
 * @error: a #GError
 *
 * Reexecute the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_reexecute (GPasteClient *self,
                          GError      **error)
{
    DBUS_CALL_NO_PARAM_NO_RETURN (REEXECUTE);
}

/**
 * g_paste_client_backup_history:
 * @self: a #GPasteClient instance
 * @name: the name of the backup
 * @error: a #GError
 *
 * Backup the current history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_backup_history (GPasteClient *self,
                               const gchar  *name,
                               GError      **error)
{
    DBUS_CALL_ONE_PARAM_NO_RETURN (BACKUP_HISTORY, string, name);
}

/**
 * g_paste_client_switch_history:
 * @self: a #GPasteClient instance
 * @name: the name of the history to switch to
 * @error: a #GError
 *
 * Switch to another history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_switch_history (GPasteClient *self,
                               const gchar  *name,
                               GError      **error)
{
    DBUS_CALL_ONE_PARAM_NO_RETURN (SWITCH_HISTORY, string, name);
}

/**
 * g_paste_client_delete_history:
 * @self: a #GPasteClient instance
 * @name: the name of the history to delete
 * @error: a #GError
 *
 * Delete an history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_delete_history (GPasteClient *self,
                               const gchar  *name,
                               GError      **error)
{
    DBUS_CALL_ONE_PARAM_NO_RETURN (DELETE_HISTORY, string, name);
}

/**
 * g_paste_client_list_histories:
 * @self: a #GPasteClient instance
 * @error: a #GError
 *
 * List all available hisotries
 *
 * Returns: (transfer full): a newly allocated array of string
 */
G_PASTE_VISIBLE GStrv
g_paste_client_list_histories (GPasteClient *self,
                               GError      **error)
{
    DBUS_CALL_NO_PARAM_RET_STRV (LIST_HISTORIES);
}

/**
 * g_paste_client_get_element_async:
 * @self: a #GPasteClient instance
 * @index: the index of the element we want to get
 * @callback: (allow-none): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Get an item from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_get_element_async (GPasteClient       *self,
                                  guint32             index,
                                  GAsyncReadyCallback callback,
                                  gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (GET_ELEMENT, uint32, index);
}

/**
 * g_paste_client_get_history_async:
 * @self: a #GPasteClient instance
 * @callback: (allow-none): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Get the history from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_get_history_async (GPasteClient       *self,
                                  GAsyncReadyCallback callback,
                                  gpointer            user_data)
{
    DBUS_CALL_NO_PARAM_ASYNC (GET_HISTORY);
}

/**
 * g_paste_client_add_async:
 * @self: a #GPasteClient instance
 * @text: the text to add
 * @callback: (allow-none): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Add an item to the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_add_async (GPasteClient       *self,
                          const gchar        *text,
                          GAsyncReadyCallback callback,
                          gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (ADD, string, text);
}

/**
 * g_paste_client_add_file_async:
 * @self: a #GPasteClient instance
 * @file: the file to add
 * @callback: (allow-none): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Add the file contents to the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_add_file_async (GPasteClient       *self,
                               const gchar        *file,
                               GAsyncReadyCallback callback,
                               gpointer            user_data)
{
    G_PASTE_CLEANUP_FREE gchar *absolute_path = NULL;

    if (!g_path_is_absolute (file))
    {
        G_PASTE_CLEANUP_FREE gchar *current_dir = g_get_current_dir ();
        absolute_path = g_build_filename (current_dir, file, NULL);
    }

    DBUS_CALL_ONE_PARAM_ASYNC (ADD_FILE, string, ((absolute_path) ? absolute_path : file));
}

/**
 * g_paste_client_select_async:
 * @self: a #GPasteClient instance
 * @index: the index of the element we want to select
 * @callback: (allow-none): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Select an item from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_select_async (GPasteClient       *self,
                             guint32             index,
                             GAsyncReadyCallback callback,
                             gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (SELECT, uint32, index);
}

/**
 * g_paste_client_delete_async:
 * @self: a #GPasteClient instance
 * @index: the index of the element we want to delete
 * @callback: (allow-none): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Delete an item from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_delete_async (GPasteClient       *self,
                             guint32             index,
                             GAsyncReadyCallback callback,
                             gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (DELETE, uint32, index);
}

/**
 * g_paste_client_empty_async:
 * @self: a #GPasteClient instance
 * @callback: (allow-none): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Empty the history from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_empty_async (GPasteClient       *self,
                            GAsyncReadyCallback callback,
                            gpointer            user_data)
{
    DBUS_CALL_NO_PARAM_ASYNC (EMPTY);
}

/**
 * g_paste_client_track_async:
 * @self: a #GPasteClient instance
 * @state: the new tracking state of the #GPasteDaemon
 * @callback: (allow-none): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Change the tracking state of the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_track_async (GPasteClient *self,
                            gboolean      state,
                            GAsyncReadyCallback callback,
                            gpointer             user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (TRACK, boolean, state);
}

/**
 * g_paste_client_on_extension_state_changed_async:
 * @self: a #GPasteClient instance
 * @state: the new state of the extension
 * @callback: (allow-none): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Call this when the extension changes its state
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_on_extension_state_changed_async (GPasteClient       *self,
                                                 gboolean            state,
                                                 GAsyncReadyCallback callback,
                                                 gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (ON_EXTENSION_STATE_CHANGED, boolean, state);
}

/**
 * g_paste_client_reexecute_async:
 * @self: a #GPasteClient instance
 * @callback: (allow-none): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Reexecute the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_reexecute_async (GPasteClient       *self,
                                GAsyncReadyCallback callback,
                                gpointer            user_data)
{
    DBUS_CALL_NO_PARAM_ASYNC (REEXECUTE);
}

/**
 * g_paste_client_backup_history_async:
 * @self: a #GPasteClient instance
 * @name: the name of the backup
 * @callback: (allow-none): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Backup the current history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_backup_history_async (GPasteClient       *self,
                                     const gchar        *name,
                                     GAsyncReadyCallback callback,
                                     gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (BACKUP_HISTORY, string, name);
}

/**
 * g_paste_client_switch_history_async:
 * @self: a #GPasteClient instance
 * @name: the name of the history to switch to
 * @callback: (allow-none): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Switch to another history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_switch_history_async (GPasteClient       *self,
                                     const gchar        *name,
                                     GAsyncReadyCallback callback,
                                     gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (SWITCH_HISTORY, string, name);
}

/**
 * g_paste_client_delete_history_async:
 * @self: a #GPasteClient instance
 * @name: the name of the history to delete
 * @callback: (allow-none): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Delete an history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_delete_history_async (GPasteClient       *self,
                                     const gchar        *name,
                                     GAsyncReadyCallback callback,
                                     gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (DELETE_HISTORY, string, name);
}

/**
 * g_paste_client_list_histories_async:
 * @self: a #GPasteClient instance
 * @callback: (allow-none): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * List all available hisotries
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_list_histories_async (GPasteClient       *self,
                                     GAsyncReadyCallback callback,
                                     gpointer            user_data)
{
    DBUS_CALL_NO_PARAM_ASYNC (LIST_HISTORIES);
}

/**
 * g_paste_client_is_active:
 * @self: a #GPasteClient instance
 *
 * Check if the daemon is active
 *
 * Returns: whether the daemon is active or not
 */
G_PASTE_VISIBLE gboolean
g_paste_client_is_active (GPasteClient *self)
{
    DBUS_GET_BOOLEAN_PROPERTY (ACTIVE);
}

static void
g_paste_client_handle_signal (GPasteClient *self,
                              gchar        *sender_name G_GNUC_UNUSED,
                              gchar        *signal_name,
                              GVariant     *parameters,
                              gpointer      user_data G_GNUC_UNUSED)
{
    HANDLE_SIGNAL (CHANGED)
    else HANDLE_SIGNAL (NAME_LOST)
    else HANDLE_SIGNAL (REEXECUTE_SELF)
    else HANDLE_SIGNAL (SHOW_HISTORY)
    else HANDLE_SIGNAL_WITH_DATA (TRACKING, gboolean, boolean)
}

static void
g_paste_client_dispose (GObject *object)
{
    GPasteClientPrivate *priv = g_paste_client_get_instance_private (G_PASTE_CLIENT (object));
    GDBusNodeInfo *g_paste_daemon_dbus_info = priv->g_paste_daemon_dbus_info;

    if (g_paste_daemon_dbus_info)
    {
        GDBusProxy *proxy = priv->proxy;

        if (proxy)
        {
            g_signal_handler_disconnect (proxy, priv->g_signal);
            g_clear_object (&priv->proxy);
        }
        g_dbus_node_info_unref (g_paste_daemon_dbus_info);
        priv->g_paste_daemon_dbus_info = NULL;
    }

    G_OBJECT_CLASS (g_paste_client_parent_class)->dispose (object);
}

static void
g_paste_client_class_init (GPasteClientClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_client_dispose;

    signals[CHANGED]        = NEW_SIGNAL ("changed");
    signals[NAME_LOST]      = NEW_SIGNAL ("name-lost");
    signals[REEXECUTE_SELF] = NEW_SIGNAL ("reexecute-self");
    signals[SHOW_HISTORY]   = NEW_SIGNAL ("show-history");
    signals[TRACKING]       = NEW_SIGNAL_WITH_DATA ("tracking", BOOLEAN);
}

static void
g_paste_client_init (GPasteClient *self)
{
    GPasteClientPrivate *priv = g_paste_client_get_instance_private (self);

    priv->g_paste_daemon_dbus_info = g_dbus_node_info_new_for_xml (G_PASTE_GDBUS_INTERFACE,
                                                                   NULL); /* Error */

    priv->connection_error = NULL;
    GDBusProxy *proxy = priv->proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                                     G_DBUS_PROXY_FLAGS_NONE,
                                                                     priv->g_paste_daemon_dbus_info->interfaces[0],
                                                                     G_PASTE_GDBUS_BUS_NAME,
                                                                     G_PASTE_GDBUS_OBJECT_PATH,
                                                                     G_PASTE_GDBUS_INTERFACE_NAME,
                                                                     NULL, /* cancellable */
                                                                     &priv->connection_error);

    if (proxy)
    {
        priv->g_signal = g_signal_connect_swapped (G_OBJECT (proxy),
                                                   "g-signal",
                                                   G_CALLBACK (g_paste_client_handle_signal),
                                                   self); /* user_data */
    }
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
g_paste_client_new (GError **error)
{
    GPasteClient *self = g_object_new (G_PASTE_TYPE_CLIENT, NULL);
    GPasteClientPrivate *priv = g_paste_client_get_instance_private (self);

    if (!priv->proxy)
    {
        if (error)
            *error = priv->connection_error;
        g_object_unref (self);
        return NULL;
    }

    return self;
}
