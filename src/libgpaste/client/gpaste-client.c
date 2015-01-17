/*
 *      This file is part of GPaste.
 *
 *      Copyright 2012-2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#define __G_PASTE_NEEDS_AU__
#include "gpaste-gdbus-macros.h"

#include <gpaste-gdbus-defines.h>

G_DEFINE_TYPE (GPasteClient, g_paste_client, G_TYPE_DBUS_PROXY)

enum
{
    CHANGED,
    NAME_LOST,
    REEXECUTE_SELF,
    SHOW_HISTORY,
    TRACKING,
    UPDATE,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/*******************/
/* Methods / Async */
/*******************/

#define DBUS_CALL_NO_PARAM_ASYNC(method) \
    DBUS_CALL_NO_PARAM_ASYNC_BASE (CLIENT, G_PASTE_DAEMON_##method)

#define DBUS_CALL_ONE_PARAM_ASYNC(method, param_type, param_name) \
    DBUS_CALL_ONE_PARAM_ASYNC_BASE (CLIENT, param_type, param_name, G_PASTE_DAEMON_##method)

#define DBUS_CALL_TWO_PARAMS_ASYNC(method, params) \
    DBUS_CALL_TWO_PARAMS_ASYNC_BASE (CLIENT, params, G_PASTE_DAEMON_##method)

/****************************/
/* Methods / Async - Finish */
/****************************/

#define DBUS_ASYNC_FINISH_NO_RETURN \
    DBUS_ASYNC_FINISH_NO_RETURN_BASE (CLIENT)

#define DBUS_ASYNC_FINISH_RET_STRING \
    DBUS_ASYNC_FINISH_RET_STRING_BASE (CLIENT)

#define DBUS_ASYNC_FINISH_RET_STRV \
    DBUS_ASYNC_FINISH_RET_STRV_BASE (CLIENT)

#define DBUS_ASYNC_FINISH_RET_UINT32 \
    DBUS_ASYNC_FINISH_RET_UINT32_BASE (CLIENT)

#define DBUS_ASYNC_FINISH_RET_AU(len) \
    DBUS_ASYNC_FINISH_RET_AU_BASE (CLIENT, len)

/******************/
/* Methods / Sync */
/******************/

#define DBUS_CALL_NO_PARAM_NO_RETURN(method) \
    DBUS_CALL_NO_PARAM_NO_RETURN_BASE (CLIENT, G_PASTE_DAEMON_##method)

#define DBUS_CALL_NO_PARAM_RET_STRV(method) \
    DBUS_CALL_NO_PARAM_RET_STRV_BASE (CLIENT, G_PASTE_DAEMON_##method)

#define DBUS_CALL_NO_PARAM_RET_UINT32(method) \
    DBUS_CALL_NO_PARAM_RET_UINT32_BASE (CLIENT, G_PASTE_DAEMON_##method)

#define DBUS_CALL_ONE_PARAM_NO_RETURN(method, param_type, param_name) \
    DBUS_CALL_ONE_PARAM_NO_RETURN_BASE (CLIENT, param_type, param_name, G_PASTE_DAEMON_##method)

#define DBUS_CALL_ONE_PARAM_RET_STRING(method, param_type, param_name) \
    DBUS_CALL_ONE_PARAM_RET_STRING_BASE (CLIENT, param_type, param_name, G_PASTE_DAEMON_##method)

#define DBUS_CALL_ONE_PARAM_RET_AU(method, param_type, param_name, len) \
    DBUS_CALL_ONE_PARAM_RET_AU_BASE (CLIENT, param_type, param_name, G_PASTE_DAEMON_##method, len)

#define DBUS_CALL_TWO_PARAMS_NO_RETURN(method, params) \
    DBUS_CALL_TWO_PARAMS_NO_RETURN_BASE (CLIENT, params, G_PASTE_DAEMON_##method)

/**************/
/* Properties */
/**************/

#define DBUS_GET_BOOLEAN_PROPERTY(property) \
    DBUS_GET_BOOLEAN_PROPERTY_BASE (CLIENT, G_PASTE_DAEMON_PROP_##property)

#define DBUS_GET_STRING_PROPERTY(property) \
    DBUS_GET_STRING_PROPERTY_BASE (CLIENT, G_PASTE_DAEMON_PROP_##property)

/***********/
/* Signals */
/***********/

#define HANDLE_SIGNAL(sig)                                  \
    if (!g_strcmp0 (signal_name, G_PASTE_DAEMON_SIG_##sig)) \
    {                                                       \
        g_signal_emit (self,                                \
                       signals[sig],                        \
                       0, /* detail */                      \
                       NULL);                               \
    }
#define HANDLE_SIGNAL_WITH_DATA(sig, ans_type, variant_type)                                        \
    if (!g_strcmp0 (signal_name, G_PASTE_DAEMON_SIG_##sig))                                         \
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

/******************/
/* Methods / Sync */
/******************/

/**
 * g_paste_client_about_sync:
 * @self: a #GPasteClient instance
 * @error: a #GError
 *
 * Display the about dialog
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_about_sync (GPasteClient *self,
                           GError      **error)
{
    DBUS_CALL_NO_PARAM_NO_RETURN (ABOUT);
}

/**
 * g_paste_client_add_sync:
 * @self: a #GPasteClient instance
 * @text: the text to add
 * @error: a #GError
 *
 * Add an item to the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_add_sync (GPasteClient *self,
                         const gchar  *text,
                         GError      **error)
{
    DBUS_CALL_ONE_PARAM_NO_RETURN (ADD, string, text);
}

/**
 * g_paste_client_add_file_sync:
 * @self: a #GPasteClient instance
 * @file: the file to add
 * @error: a #GError
 *
 * Add the file contents to the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_add_file_sync (GPasteClient *self,
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
 * g_paste_client_add_password_sync:
 * @self: a #GPasteClient instance
 * @name: the name to identify the password to add
 * @password: the password to add
 * @error: a #GError
 *
 * Add the password to the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_add_password_sync (GPasteClient *self,
                                  const gchar  *name,
                                  const gchar  *password,
                                  GError      **error)
{
    GVariant *params[] = {
        g_variant_new_string (name),
        g_variant_new_string (password)
    };

    DBUS_CALL_TWO_PARAMS_NO_RETURN (ADD_PASSWORD, params);
}

/**
 * g_paste_client_backup_history_sync:
 * @self: a #GPasteClient instance
 * @name: the name of the backup
 * @error: a #GError
 *
 * Backup the current history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_backup_history_sync (GPasteClient *self,
                                    const gchar  *name,
                                    GError      **error)
{
    DBUS_CALL_ONE_PARAM_NO_RETURN (BACKUP_HISTORY, string, name);
}

/**
 * g_paste_client_delete_sync:
 * @self: a #GPasteClient instance
 * @index: the index of the element we want to delete
 * @error: a #GError
 *
 * Delete an item from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_delete_sync (GPasteClient *self,
                            guint32       index,
                            GError      **error)
{
    DBUS_CALL_ONE_PARAM_NO_RETURN (DELETE, uint32, index);
}

/**
 * g_paste_client_delete_history_sync:
 * @self: a #GPasteClient instance
 * @name: the name of the history to delete
 * @error: a #GError
 *
 * Delete an history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_delete_history_sync (GPasteClient *self,
                                    const gchar  *name,
                                    GError      **error)
{
    DBUS_CALL_ONE_PARAM_NO_RETURN (DELETE_HISTORY, string, name);
}

/**
 * g_paste_client_delete_password_sync:
 * @self: a #GPasteClient instance
 * @name: the name of the password to delete
 * @error: a #GError
 *
 * Delete the password from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_delete_password_sync (GPasteClient *self,
                                     const gchar  *name,
                                     GError      **error)
{
    DBUS_CALL_ONE_PARAM_NO_RETURN (DELETE_PASSWORD, string, name);
}

/**
 * g_paste_client_empty_sync:
 * @self: a #GPasteClient instance
 * @error: a #GError
 *
 * Empty the history from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_empty_sync (GPasteClient *self,
                           GError      **error)
{
    DBUS_CALL_NO_PARAM_NO_RETURN (EMPTY);
}

/**
 * g_paste_client_get_element_sync:
 * @self: a #GPasteClient instance
 * @index: the index of the element we want to get
 * @error: a #GError
 *
 * Get an item from the #GPasteDaemon
 *
 * Returns: a newly allocated string
 */
G_PASTE_VISIBLE gchar *
g_paste_client_get_element_sync (GPasteClient *self,
                                 guint32       index,
                                 GError      **error)
{
    DBUS_CALL_ONE_PARAM_RET_STRING (GET_ELEMENT, uint32, index);
}

/**
 * g_paste_client_get_history_sync:
 * @self: a #GPasteClient instance
 * @error: a #GError
 *
 * Get the history from the #GPasteDaemon
 *
 * Returns: (transfer full): a newly allocated array of string
 */
G_PASTE_VISIBLE GStrv
g_paste_client_get_history_sync (GPasteClient *self,
                                 GError      **error)
{
    DBUS_CALL_NO_PARAM_RET_STRV (GET_HISTORY);
}

/**
 * g_paste_client_get_history_size_sync:
 * @self: a #GPasteClient instance
 * @error: a #GError
 *
 * Get the history size from the #GPasteDaemon
 *
 * Returns: the size of the history
 */
G_PASTE_VISIBLE guint32
g_paste_client_get_history_size_sync (GPasteClient *self,
                                      GError      **error)
{
    DBUS_CALL_NO_PARAM_RET_UINT32 (GET_HISTORY_SIZE);
}

/**
 * g_paste_client_get_raw_element_sync:
 * @self: a #GPasteClient instance
 * @index: the index of the element we want to get
 * @error: a #GError
 *
 * Get an item from the #GPasteDaemon
 *
 * Returns: a newly allocated string
 */
G_PASTE_VISIBLE gchar *
g_paste_client_get_raw_element_sync (GPasteClient *self,
                                     guint32       index,
                                     GError      **error)
{
    DBUS_CALL_ONE_PARAM_RET_STRING (GET_RAW_ELEMENT, uint32, index);
}

/**
 * g_paste_client_get_raw_history_sync:
 * @self: a #GPasteClient instance
 * @error: a #GError
 *
 * Get the history from the #GPasteDaemon
 *
 * Returns: (transfer full): a newly allocated array of string
 */
G_PASTE_VISIBLE GStrv
g_paste_client_get_raw_history_sync (GPasteClient *self,
                                     GError      **error)
{
    DBUS_CALL_NO_PARAM_RET_STRV (GET_RAW_HISTORY);
}

/**
 * g_paste_client_list_histories_sync:
 * @self: a #GPasteClient instance
 * @error: a #GError
 *
 * List all available hisotries
 *
 * Returns: (transfer full): a newly allocated array of string
 */
G_PASTE_VISIBLE GStrv
g_paste_client_list_histories_sync (GPasteClient *self,
                                    GError      **error)
{
    DBUS_CALL_NO_PARAM_RET_STRV (LIST_HISTORIES);
}

/**
 * g_paste_client_on_extension_state_changed_sync:
 * @self: a #GPasteClient instance
 * @state: the new state of the extension
 * @error: a #GError
 *
 * Call this when the extension changes its state
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_on_extension_state_changed_sync (GPasteClient *self,
                                                gboolean      state,
                                                GError      **error)
{
    DBUS_CALL_ONE_PARAM_NO_RETURN (ON_EXTENSION_STATE_CHANGED, boolean, state);
}

/**
 * g_paste_client_reexecute_sync:
 * @self: a #GPasteClient instance
 * @error: a #GError
 *
 * Reexecute the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_reexecute_sync (GPasteClient *self,
                               GError      **error)
{
    DBUS_CALL_NO_PARAM_NO_RETURN (REEXECUTE);
}

/**
 * g_paste_client_rename_password_sync:
 * @self: a #GPasteClient instance
 * @old_name: the name of the password to rename
 * @new_name: the new name to give it
 * @error: a #GError
 *
 * Rename the password in the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_rename_password_sync (GPasteClient *self,
                                     const gchar  *old_name,
                                     const gchar  *new_name,
                                     GError      **error)
{
    GVariant *params[] = {
        g_variant_new_string (old_name),
        g_variant_new_string (new_name)
    };

    DBUS_CALL_TWO_PARAMS_NO_RETURN (RENAME_PASSWORD, params);
}

/**
 * g_paste_client_search_sync:
 * @self: a #GPasteClient instance
 * @pattern: the pattern to look for in history
 * @hits: (out) (optional): number of hits
 * @error: a #GError
 *
 * Search for items matching @pattern in history
 *
 * Returns: (array length=hits): The indexes of the matching items
 */
G_PASTE_VISIBLE guint32 *
g_paste_client_search_sync (GPasteClient *self,
                            const gchar  *pattern,
                            gsize        *hits,
                            GError      **error)
{
    DBUS_CALL_ONE_PARAM_RET_AU (SEARCH, string, pattern, hits);
}

/**
 * g_paste_client_select_sync:
 * @self: a #GPasteClient instance
 * @index: the index of the element we want to select
 * @error: a #GError
 *
 * Select an item from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_select_sync (GPasteClient *self,
                            guint32       index,
                            GError      **error)
{
    DBUS_CALL_ONE_PARAM_NO_RETURN (SELECT, uint32, index);
}

/**
 * g_paste_client_set_password_sync:
 * @self: a #GPasteClient instance
 * @index: the index of the element we want to set as password
 * @name: the name to identify the password
 * @error: a #GError
 *
 * Set the item as password
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_set_password_sync (GPasteClient *self,
                                  guint32       index,
                                  const gchar  *name,
                                  GError      **error)
{
    GVariant *params[] = {
        g_variant_new_uint32 (index),
        g_variant_new_string (name)
    };

    DBUS_CALL_TWO_PARAMS_NO_RETURN (SET_PASSWORD, params);
}

/**
 * g_paste_client_show_history_sync:
 * @self: a #GPasteClient instance
 * @error: a #GError
 *
 * Emit the ShowHistory signal
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_show_history_sync (GPasteClient *self,
                                  GError      **error)
{
    DBUS_CALL_NO_PARAM_NO_RETURN (SHOW_HISTORY);
}
/**
 * g_paste_client_switch_history_sync:
 * @self: a #GPasteClient instance
 * @name: the name of the history to switch to
 * @error: a #GError
 *
 * Switch to another history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_switch_history_sync (GPasteClient *self,
                                    const gchar  *name,
                                    GError      **error)
{
    DBUS_CALL_ONE_PARAM_NO_RETURN (SWITCH_HISTORY, string, name);
}

/**
 * g_paste_client_track_sync:
 * @self: a #GPasteClient instance
 * @state: the new tracking state of the #GPasteDaemon
 * @error: a #GError
 *
 * Change the tracking state of the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_track_sync (GPasteClient *self,
                           gboolean      state,
                           GError      **error)
{
    DBUS_CALL_ONE_PARAM_NO_RETURN (TRACK, boolean, state);
}

/*******************/
/* Methods / Async */
/*******************/

/**
 * g_paste_client_about:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Display the about dialog
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_about (GPasteClient       *self,
                      GAsyncReadyCallback callback,
                      gpointer            user_data)
{
    DBUS_CALL_NO_PARAM_ASYNC (ABOUT);
}

/**
 * g_paste_client_add:
 * @self: a #GPasteClient instance
 * @text: the text to add
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Add an item to the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_add (GPasteClient       *self,
                    const gchar        *text,
                    GAsyncReadyCallback callback,
                    gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (ADD, string, text);
}

/**
 * g_paste_client_add_file:
 * @self: a #GPasteClient instance
 * @file: the file to add
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Add the file contents to the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_add_file (GPasteClient       *self,
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
 * g_paste_client_add_password:
 * @self: a #GPasteClient instance
 * @name: the name to identify the password to add
 * @password: the password to add
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Add the password to the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_add_password (GPasteClient       *self,
                             const gchar        *name,
                             const gchar        *password,
                             GAsyncReadyCallback callback,
                             gpointer            user_data)
{
    GVariant *params[] = {
        g_variant_new_string (name),
        g_variant_new_string (password)
    };

    DBUS_CALL_TWO_PARAMS_ASYNC (ADD_PASSWORD, params);
}

/**
 * g_paste_client_backup_history:
 * @self: a #GPasteClient instance
 * @name: the name of the backup
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Backup the current history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_backup_history (GPasteClient       *self,
                               const gchar        *name,
                               GAsyncReadyCallback callback,
                               gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (BACKUP_HISTORY, string, name);
}

/**
 * g_paste_client_delete:
 * @self: a #GPasteClient instance
 * @index: the index of the element we want to delete
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Delete an item from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_delete (GPasteClient       *self,
                       guint32             index,
                       GAsyncReadyCallback callback,
                       gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (DELETE, uint32, index);
}

/**
 * g_paste_client_delete_history:
 * @self: a #GPasteClient instance
 * @name: the name of the history to delete
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Delete an history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_delete_history (GPasteClient       *self,
                               const gchar        *name,
                               GAsyncReadyCallback callback,
                               gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (DELETE_HISTORY, string, name);
}

/**
 * g_paste_client_delete_password:
 * @self: a #GPasteClient instance
 * @name: the name of the password to delete
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Delete the password from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_delete_password (GPasteClient       *self,
                                const gchar        *name,
                                GAsyncReadyCallback callback,
                                gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (DELETE_PASSWORD, string, name);
}

/**
 * g_paste_client_empty:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Empty the history from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_empty (GPasteClient       *self,
                      GAsyncReadyCallback callback,
                      gpointer            user_data)
{
    DBUS_CALL_NO_PARAM_ASYNC (EMPTY);
}

/**
 * g_paste_client_get_element:
 * @self: a #GPasteClient instance
 * @index: the index of the element we want to get
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get an item from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_get_element (GPasteClient       *self,
                            guint32             index,
                            GAsyncReadyCallback callback,
                            gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (GET_ELEMENT, uint32, index);
}

/**
 * g_paste_client_get_history:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get the history from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_get_history (GPasteClient       *self,
                            GAsyncReadyCallback callback,
                            gpointer            user_data)
{
    DBUS_CALL_NO_PARAM_ASYNC (GET_HISTORY);
}

/**
 * g_paste_client_get_history_size:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get the history isize from the #GPasteDaemon
 *
 * Returns:
 */

G_PASTE_VISIBLE void
g_paste_client_get_history_size (GPasteClient       *self,
                                 GAsyncReadyCallback callback,
                                 gpointer            user_data)
{
    DBUS_CALL_NO_PARAM_ASYNC (GET_HISTORY_SIZE);
}

/**
 * g_paste_client_get_raw_element:
 * @self: a #GPasteClient instance
 * @index: the index of the element we want to get
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get an item from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_get_raw_element (GPasteClient       *self,
                                guint32             index,
                                GAsyncReadyCallback callback,
                                gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (GET_RAW_ELEMENT, uint32, index);
}

/**
 * g_paste_client_get_raw_history:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get the history from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_get_raw_history (GPasteClient       *self,
                                GAsyncReadyCallback callback,
                                gpointer            user_data)
{
    DBUS_CALL_NO_PARAM_ASYNC (GET_RAW_HISTORY);
}

/**
 * g_paste_client_list_histories:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * List all available hisotries
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_list_histories (GPasteClient       *self,
                               GAsyncReadyCallback callback,
                               gpointer            user_data)
{
    DBUS_CALL_NO_PARAM_ASYNC (LIST_HISTORIES);
}

/**
 * g_paste_client_on_extension_state_changed:
 * @self: a #GPasteClient instance
 * @state: the new state of the extension
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Call this when the extension changes its state
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_on_extension_state_changed (GPasteClient       *self,
                                           gboolean            state,
                                           GAsyncReadyCallback callback,
                                           gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (ON_EXTENSION_STATE_CHANGED, boolean, state);
}

/**
 * g_paste_client_reexecute:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Reexecute the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_reexecute (GPasteClient       *self,
                          GAsyncReadyCallback callback,
                          gpointer            user_data)
{
    DBUS_CALL_NO_PARAM_ASYNC (REEXECUTE);
}

/**
 * g_paste_client_rename_password:
 * @self: a #GPasteClient instance
 * @old_name: the old name of the password to rename
 * @new_name: the new name to give it
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Rename the password in the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_rename_password (GPasteClient       *self,
                                const gchar        *old_name,
                                const gchar        *new_name,
                                GAsyncReadyCallback callback,
                                gpointer            user_data)
{
    GVariant *params[] = {
        g_variant_new_string (old_name),
        g_variant_new_string (new_name)
    };

    DBUS_CALL_TWO_PARAMS_ASYNC (RENAME_PASSWORD, params);
}

/**
 * g_paste_client_search:
 * @self: a #GPasteClient instance
 * @pattern: the pattern to look for in history
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Search for items matching @pattern in history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_search (GPasteClient       *self,
                       const gchar        *pattern,
                       GAsyncReadyCallback callback,
                       gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (SEARCH, string, pattern);
}

/**
 * g_paste_client_select:
 * @self: a #GPasteClient instance
 * @index: the index of the element we want to select
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Select an item from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_select (GPasteClient       *self,
                       guint32             index,
                       GAsyncReadyCallback callback,
                       gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (SELECT, uint32, index);
}

/**
 * g_paste_client_set_password:
 * @self: a #GPasteClient instance
 * @index: the index of the element we want to set as password
 * @name: the name to identify the password
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Set the item as password
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_set_password (GPasteClient       *self,
                             guint32             index,
                             const gchar        *name,
                             GAsyncReadyCallback callback,
                             gpointer            user_data)
{
    GVariant *params[] = {
        g_variant_new_uint32 (index),
        g_variant_new_string (name)
    };

    DBUS_CALL_TWO_PARAMS_ASYNC (SET_PASSWORD, params);
}

/**
 * g_paste_client_show_history:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Emit the ShowHistory signal
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_show_history (GPasteClient       *self,
                             GAsyncReadyCallback callback,
                             gpointer            user_data)
{
    DBUS_CALL_NO_PARAM_ASYNC (SHOW_HISTORY);
}

/**
 * g_paste_client_switch_history:
 * @self: a #GPasteClient instance
 * @name: the name of the history to switch to
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable):The data to pass to @callback.
 *
 * Switch to another history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_switch_history (GPasteClient       *self,
                               const gchar        *name,
                               GAsyncReadyCallback callback,
                               gpointer            user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (SWITCH_HISTORY, string, name);
}

/**
 * g_paste_client_track:
 * @self: a #GPasteClient instance
 * @state: the new tracking state of the #GPasteDaemon
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Change the tracking state of the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_track (GPasteClient *self,
                      gboolean      state,
                      GAsyncReadyCallback callback,
                      gpointer             user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (TRACK, boolean, state);
}

/****************************/
/* Methods / Async - Finish */
/****************************/

/**
 * g_paste_client_about_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Display the about dialog
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_about_finish (GPasteClient *self,
                             GAsyncResult *result,
                             GError      **error)
{
    DBUS_ASYNC_FINISH_NO_RETURN;
}

/**
 * g_paste_client_add_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Add an item to the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_add_finish (GPasteClient *self,
                           GAsyncResult *result,
                           GError      **error)
{
    DBUS_ASYNC_FINISH_NO_RETURN;
}

/**
 * g_paste_client_add_file_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Add the file contents to the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_add_file_finish (GPasteClient *self,
                                GAsyncResult *result,
                                GError      **error)
{
    DBUS_ASYNC_FINISH_NO_RETURN;
}

/**
 * g_paste_client_add_password_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Add the password to the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_add_password_finish (GPasteClient *self,
                                    GAsyncResult *result,
                                    GError      **error)
{
    DBUS_ASYNC_FINISH_NO_RETURN;
}

/**
 * g_paste_client_backup_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Backup the current history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_backup_history_finish (GPasteClient *self,
                                      GAsyncResult *result,
                                      GError      **error)
{
    DBUS_ASYNC_FINISH_NO_RETURN;
}

/**
 * g_paste_client_delete_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Delete an item from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_delete_finish (GPasteClient *self,
                              GAsyncResult *result,
                              GError      **error)
{
    DBUS_ASYNC_FINISH_NO_RETURN;
}

/**
 * g_paste_client_delete_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Delete an history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_delete_history_finish (GPasteClient *self,
                                      GAsyncResult *result,
                                      GError      **error)
{
    DBUS_ASYNC_FINISH_NO_RETURN;
}

/**
 * g_paste_client_delete_password_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Delete the password from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_delete_password_finish (GPasteClient *self,
                                       GAsyncResult *result,
                                       GError      **error)
{
    DBUS_ASYNC_FINISH_NO_RETURN;
}

/**
 * g_paste_client_empty_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Empty the history from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_empty_finish (GPasteClient *self,
                             GAsyncResult *result,
                             GError      **error)
{
    DBUS_ASYNC_FINISH_NO_RETURN;
}

/**
 * g_paste_client_get_element_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Get an item from the #GPasteDaemon
 *
 * Returns: a newly allocated string
 */
G_PASTE_VISIBLE gchar *
g_paste_client_get_element_finish (GPasteClient *self,
                                   GAsyncResult *result,
                                   GError      **error)
{
    DBUS_ASYNC_FINISH_RET_STRING;
}

/**
 * g_paste_client_get_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Get the history from the #GPasteDaemon
 *
 * Returns: (transfer full): a newly allocated array of string
 */
G_PASTE_VISIBLE GStrv
g_paste_client_get_history_finish (GPasteClient *self,
                                   GAsyncResult *result,
                                   GError      **error)
{
    DBUS_ASYNC_FINISH_RET_STRV;
}

/**
 * g_paste_client_get_history_size_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Get the history size from the #GPasteDaemon
 *
 * Returns: the size of the history
 */
G_PASTE_VISIBLE guint32
g_paste_client_get_history_size_finish (GPasteClient *self,
                                        GAsyncResult *result,
                                        GError      **error)
{
    DBUS_ASYNC_FINISH_RET_UINT32;
}

/**
 * g_paste_client_get_raw_element_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Get an item from the #GPasteDaemon
 *
 * Returns: a newly allocated string
 */
G_PASTE_VISIBLE gchar *
g_paste_client_get_raw_element_finish (GPasteClient *self,
                                       GAsyncResult *result,
                                       GError      **error)
{
    DBUS_ASYNC_FINISH_RET_STRING;
}

/**
 * g_paste_client_get_raw_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Get the history from the #GPasteDaemon
 *
 * Returns: (transfer full): a newly allocated array of string
 */
G_PASTE_VISIBLE GStrv
g_paste_client_get_raw_history_finish (GPasteClient *self,
                                       GAsyncResult *result,
                                       GError      **error)
{
    DBUS_ASYNC_FINISH_RET_STRV;
}

/**
 * g_paste_client_list_histories_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * List all available hisotries
 *
 * Returns: (transfer full): a newly allocated array of string
 */
G_PASTE_VISIBLE GStrv
g_paste_client_list_histories_finish (GPasteClient *self,
                                      GAsyncResult *result,
                                      GError      **error)
{
    DBUS_ASYNC_FINISH_RET_STRV;
}

/**
 * g_paste_client_on_extension_state_changed_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Call this when the extension changes its state
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_on_extension_state_changed_finish (GPasteClient *self,
                                                  GAsyncResult *result,
                                                  GError      **error)
{
    DBUS_ASYNC_FINISH_NO_RETURN;
}

/**
 * g_paste_client_reexecute_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Reexecute the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_reexecute_finish (GPasteClient *self,
                                 GAsyncResult *result,
                                 GError      **error)
{
    DBUS_ASYNC_FINISH_NO_RETURN;
}

/**
 * g_paste_client_rename_password_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Rename the password in the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_rename_password_finish (GPasteClient *self,
                                       GAsyncResult *result,
                                       GError      **error)
{
    DBUS_ASYNC_FINISH_NO_RETURN;
}

/**
 * g_paste_client_search_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @hits: (out) (optional): number of hits
 * @error: a #GError
 *
 * Search for items matching @pattern in history
 *
 * Returns: (array length=hits): The indexes of the matching items
 */
G_PASTE_VISIBLE guint32 *
g_paste_client_search_finish (GPasteClient *self,
                              GAsyncResult *result,
                              gsize        *hits,
                              GError      **error)
{
    DBUS_ASYNC_FINISH_RET_AU (hits);
}

/**
 * g_paste_client_select_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Select an item from the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_select_finish (GPasteClient *self,
                              GAsyncResult *result,
                              GError      **error)
{
    DBUS_ASYNC_FINISH_NO_RETURN;
}

/**
 * g_paste_client_set_password_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Set the item as password
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_set_password_finish (GPasteClient *self,
                                    GAsyncResult *result,
                                    GError      **error)
{
    DBUS_ASYNC_FINISH_NO_RETURN;
}

/**
 * g_paste_client_show_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Emit the ShowHistory signal
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_show_history_finish (GPasteClient *self,
                                    GAsyncResult *result,
                                    GError      **error)
{
    DBUS_ASYNC_FINISH_NO_RETURN;
}

/**
 * g_paste_client_switch_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Switch to another history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_switch_history_finish (GPasteClient *self,
                                      GAsyncResult *result,
                                      GError      **error)
{
    DBUS_ASYNC_FINISH_NO_RETURN;
}

/**
 * g_paste_client_track_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Change the tracking state of the #GPasteDaemon
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_track_finish (GPasteClient *self,
                             GAsyncResult *result,
                             GError      **error)
{
    DBUS_ASYNC_FINISH_NO_RETURN;
}

/**************/
/* Properties */
/**************/

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

/**
 * g_paste_client_get_version:
 * @self: a #GPasteClient instance
 *
 * Get the version of the running gpaste daemon
 *
 * Returns: the version of the daemon
 */
G_PASTE_VISIBLE gchar *
g_paste_client_get_version (GPasteClient *self)
{
    DBUS_GET_STRING_PROPERTY (VERSION);
}

static void
g_paste_client_g_signal (GDBusProxy  *proxy,
                         const gchar *sender_name G_GNUC_UNUSED,
                         const gchar *signal_name,
                         GVariant    *parameters)
{
    GPasteClient *self = G_PASTE_CLIENT (proxy);

    HANDLE_SIGNAL (CHANGED)
    else HANDLE_SIGNAL (NAME_LOST)
    else HANDLE_SIGNAL (REEXECUTE_SELF)
    else HANDLE_SIGNAL (SHOW_HISTORY)
    else HANDLE_SIGNAL_WITH_DATA (TRACKING, gboolean, boolean)
    else if (!g_strcmp0 (signal_name, G_PASTE_DAEMON_SIG_UPDATE))
    {
        GVariantIter params_iter;
        g_variant_iter_init (&params_iter, parameters);
        G_PASTE_CLEANUP_VARIANT_UNREF GVariant *v1 = g_variant_iter_next_value (&params_iter);
        G_PASTE_CLEANUP_VARIANT_UNREF GVariant *v2 = g_variant_iter_next_value (&params_iter);
        G_PASTE_CLEANUP_VARIANT_UNREF GVariant *v3 = g_variant_iter_next_value (&params_iter);
        g_signal_emit (self,
                       signals[UPDATE],
                       0, /* detail */
                       g_enum_get_value_by_nick (g_type_class_peek (G_PASTE_TYPE_UPDATE_ACTION), g_variant_get_string (v1, NULL))->value,
                       g_enum_get_value_by_nick (g_type_class_peek (G_PASTE_TYPE_UPDATE_TARGET), g_variant_get_string (v2, NULL))->value,
                       g_variant_get_uint32 (v3),
                       NULL);
    }
}

static void
g_paste_client_class_init (GPasteClientClass *klass)
{
    G_DBUS_PROXY_CLASS (klass)->g_signal = g_paste_client_g_signal;

    signals[CHANGED]        = NEW_SIGNAL ("changed");
    signals[NAME_LOST]      = NEW_SIGNAL ("name-lost");
    signals[REEXECUTE_SELF] = NEW_SIGNAL ("reexecute-self");
    signals[SHOW_HISTORY]   = NEW_SIGNAL ("show-history");
    signals[TRACKING]       = NEW_SIGNAL_WITH_DATA ("tracking", BOOLEAN);
    signals[UPDATE]         = g_signal_new ("update",
                                            G_PASTE_TYPE_CLIENT,
                                            G_SIGNAL_RUN_LAST,
                                            0, /* class offset */
                                            NULL, /* accumulator */
                                            NULL, /* accumulator data */
                                            g_cclosure_marshal_generic,
                                            G_TYPE_NONE,
                                            3, /* number of params */
                                            G_PASTE_TYPE_UPDATE_ACTION,
                                            G_PASTE_TYPE_UPDATE_TARGET,
                                            G_TYPE_UINT);
}

static void
g_paste_client_init (GPasteClient *self)
{
    GDBusProxy *proxy = G_DBUS_PROXY (self);

    G_PASTE_CLEANUP_NODE_INFO_UNREF GDBusNodeInfo *g_paste_daemon_dbus_info = g_dbus_node_info_new_for_xml (G_PASTE_DAEMON_INTERFACE,
                                                                                                            NULL); /* Error */
    g_dbus_proxy_set_interface_info (proxy, g_paste_daemon_dbus_info->interfaces[0]);
}

/**
 * g_paste_client_new_sync:
 * @error: Return location for error or %NULL.
 *
 * Create a new instance of #GPasteClient
 *
 * Returns: a newly allocated #GPasteClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClient *
g_paste_client_new_sync (GError **error)
{
    CUSTOM_PROXY_NEW (CLIENT, DAEMON);
}

/**
 * g_paste_client_new:
 * @callback: Callback function to invoke when the proxy is ready.
 * @user_data: User data to pass to @callback.
 *
 * Create a new instance of #GPasteClient
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_client_new (GAsyncReadyCallback callback,
                    gpointer            user_data)
{
    CUSTOM_PROXY_NEW_ASYNC (CLIENT, DAEMON);
}

/**
 * g_paste_client_new_finish:
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback function passed to the async ctor.
 * @error: Return location for error or %NULL.
 *
 * Create a new instance of #GPasteClient
 *
 * Returns: a newly allocated #GPasteClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClient *
g_paste_client_new_finish (GAsyncResult *result,
                           GError      **error)
{
    CUSTOM_PROXY_NEW_FINISH (CLIENT);
}
