// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-daemon3.h>
#include <gpaste-3/gpaste-error.h>
#include <gpaste-3/gpaste-gdbus-defines.h>
#include <gpaste-3/gpaste-util.h>
#include <gpaste-3/gpaste-update-enums.h>

struct _GPasteClient
{
    GDBusProxy parent_instance;
};

/**
 * GPasteClient:
 *
 * A proxy for the GPaste daemon's D-Bus interface.
 *
 * Every method comes in a synchronous flavor and an async pair, and both report
 * failures the same way, in one of two kinds of domain:
 *
 * - %G_PASTE_ERROR for a request the daemon understood and refused, e.g.
 *   %G_PASTE_ERROR_NOT_FOUND for an unknown uuid. The daemon registers the
 *   domain with g_dbus_error_register_error_domain(), so the code survives the
 *   round-trip and callers can switch on it instead of matching on the message.
 * - %G_DBUS_ERROR or %G_IO_ERROR for a failure of the transport itself: no
 *   daemon on the bus, it went away mid-call, the call was cancelled. These say
 *   nothing about the request.
 *
 * A caller that wants to tell "the daemon said no" from "the daemon is not
 * there" should therefore check the domain with g_error_matches(), not the bare
 * code: the numbering of the two overlaps.
 */
static void g_paste_client_daemon3_iface_init (GPasteDaemon3Iface *iface);

G_PASTE_DEFINE_TYPE_WITH_INTERFACE (Client, client, G_TYPE_DBUS_PROXY, G_TYPE_PASTE_DAEMON3, g_paste_client_daemon3_iface_init)

/* The ids g_paste_daemon3_override_properties() hands out, in the order the
 * interface declares them. */
enum
{
    PROP_ACTIVE = 1,
    PROP_HISTORY,
    PROP_VERSION,
};

enum
{
    DELETE_HISTORY,
    EMPTY_HISTORY,
    HISTORIES_CHANGED,
    SHOW_HISTORY,
    TRACKING,
    UPDATE,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/***********/
/* Signals */
/***********/

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
#define NEW_SIGNAL_WITH_DATA_GENERIC(name, type)   \
    g_signal_new (name,                            \
                  G_PASTE_TYPE_CLIENT,             \
                  G_SIGNAL_RUN_LAST,               \
                  0, /* class offset */            \
                  NULL, /* accumulator */          \
                  NULL, /* accumulator data */     \
                  g_cclosure_marshal_generic,      \
                  G_TYPE_NONE,                     \
                  1,                               \
                  G_TYPE_##type)

/*******************************/
/* Methods                     */
/*******************************/

/*
 * Every method the daemon exposes is reached through the same three functions:
 * the synchronous call, the asynchronous one, and the finish that unpacks its
 * reply. gdbus-codegen already wrote the marshalling; what is left over is the
 * precondition checks, the flags/timeout/cancellable triple GPaste always
 * passes the same way, and -- where there is a reply -- turning the out
 * parameter into what the public API returns.
 *
 * PARAMS is the method's own parameters and ARGS the matching arguments to
 * forward. Both are parenthesized, so the preprocessor takes each as one macro
 * argument; ARGLIST supplies the comma joining them to @self, and disappears
 * when the list is empty, which is what lets a method with no parameters of its
 * own use these macros too. A finish never takes parameters of its own, so it
 * only ever needs the name.
 *
 * The gtk-doc blocks stay above the invocation, the way they sit above the
 * SETTING macros in gpaste-settings.c: g-ir-scanner matches a block to a symbol
 * by the name on its first line, not by what follows it.
 */
#define ARGLIST(...) , ##__VA_ARGS__

#define G_PASTE_CLIENT_METHOD(name, PARAMS, ARGS)                                               \
    G_PASTE_VISIBLE void                                                                        \
    g_paste_client_##name##_sync (GPasteClient *self ARGLIST PARAMS,                            \
                                  GError      **error)                                          \
    {                                                                                           \
        g_return_if_fail (G_PASTE_IS_CLIENT (self));                                            \
        g_return_if_fail (!error || !(*error));                                                 \
                                                                                                \
        g_paste_daemon3_call_##name##_sync (G_PASTE_DAEMON3 (self) ARGLIST ARGS,                \
                                            G_DBUS_CALL_FLAGS_NONE,                             \
                                            -1, /* timeout */                                   \
                                            NULL, /* cancellable */                             \
                                            error);                                             \
    }                                                                                           \
    G_PASTE_VISIBLE void                                                                        \
    g_paste_client_##name (GPasteClient       *self ARGLIST PARAMS,                             \
                           GAsyncReadyCallback callback,                                        \
                           gpointer            user_data)                                       \
    {                                                                                           \
        g_return_if_fail (G_PASTE_IS_CLIENT (self));                                            \
                                                                                                \
        g_paste_daemon3_call_##name (G_PASTE_DAEMON3 (self) ARGLIST ARGS,                       \
                                     G_DBUS_CALL_FLAGS_NONE,                                    \
                                     -1, /* timeout */                                          \
                                     NULL, /* cancellable */                                    \
                                     callback,                                                  \
                                     user_data);                                                \
    }                                                                                           \
    G_PASTE_VISIBLE void                                                                        \
    g_paste_client_##name##_finish (GPasteClient *self,                                         \
                                    GAsyncResult *result,                                       \
                                    GError      **error)                                        \
    {                                                                                           \
        g_return_if_fail (G_PASTE_IS_CLIENT (self));                                            \
        g_return_if_fail (G_IS_ASYNC_RESULT (result));                                          \
        g_return_if_fail (!error || !(*error));                                                 \
                                                                                                \
        g_paste_daemon3_call_##name##_finish (G_PASTE_DAEMON3 (self), result, error);           \
    }

/* Same, for a method that answers something: @decl declares the out parameter,
 * @out passes it, @ret turns it into the return value and @fail is what every
 * bail-out returns. */
#define G_PASTE_CLIENT_METHOD_RET(name, type, fail, decl, out, ret, PARAMS, ARGS)               \
    G_PASTE_VISIBLE type                                                                        \
    g_paste_client_##name##_sync (GPasteClient *self ARGLIST PARAMS,                            \
                                  GError      **error)                                          \
    {                                                                                           \
        g_return_val_if_fail (G_PASTE_IS_CLIENT (self), fail);                                  \
        g_return_val_if_fail (!error || !(*error), fail);                                       \
                                                                                                \
        decl;                                                                                   \
                                                                                                \
        if (!g_paste_daemon3_call_##name##_sync (G_PASTE_DAEMON3 (self) ARGLIST ARGS,           \
                                                 G_DBUS_CALL_FLAGS_NONE,                        \
                                                 -1, /* timeout */                              \
                                                 out,                                           \
                                                 NULL, /* cancellable */                        \
                                                 error))                                        \
            return fail;                                                                        \
                                                                                                \
        return ret;                                                                             \
    }                                                                                           \
    G_PASTE_VISIBLE void                                                                        \
    g_paste_client_##name (GPasteClient       *self ARGLIST PARAMS,                             \
                           GAsyncReadyCallback callback,                                        \
                           gpointer            user_data)                                       \
    {                                                                                           \
        g_return_if_fail (G_PASTE_IS_CLIENT (self));                                            \
                                                                                                \
        g_paste_daemon3_call_##name (G_PASTE_DAEMON3 (self) ARGLIST ARGS,                       \
                                     G_DBUS_CALL_FLAGS_NONE,                                    \
                                     -1, /* timeout */                                          \
                                     NULL, /* cancellable */                                    \
                                     callback,                                                  \
                                     user_data);                                                \
    }                                                                                           \
    G_PASTE_VISIBLE type                                                                        \
    g_paste_client_##name##_finish (GPasteClient *self,                                         \
                                    GAsyncResult *result,                                       \
                                    GError      **error)                                        \
    {                                                                                           \
        g_return_val_if_fail (G_PASTE_IS_CLIENT (self), fail);                                  \
        g_return_val_if_fail (G_IS_ASYNC_RESULT (result), fail);                                \
        g_return_val_if_fail (!error || !(*error), fail);                                       \
                                                                                                \
        decl;                                                                                   \
                                                                                                \
        if (!g_paste_daemon3_call_##name##_finish (G_PASTE_DAEMON3 (self), out, result, error)) \
            return fail;                                                                        \
                                                                                                \
        return ret;                                                                             \
    }

/**
 * g_paste_client_show_about_sync:
 * @self: a #GPasteClient instance
 * @error: return location for a #GError, or %NULL
 *
 * Display the about dialog
 */
/**
 * g_paste_client_show_about:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Display the about dialog
 */
/**
 * g_paste_client_show_about_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Display the about dialog
 */
G_PASTE_CLIENT_METHOD (show_about,
                       (), ())

/**
 * g_paste_client_add_text_sync:
 * @self: a #GPasteClient instance
 * @text: the text to add
 * @error: return location for a #GError, or %NULL
 *
 * Add an item to the #GPasteDaemon
 */
/**
 * g_paste_client_add_text:
 * @self: a #GPasteClient instance
 * @text: the text to add
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Add an item to the #GPasteDaemon
 */
/**
 * g_paste_client_add_text_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Add an item to the #GPasteDaemon
 */
G_PASTE_CLIENT_METHOD (add_text,
                       (const gchar *text), (text))

/**
 * g_paste_client_add_file_sync:
 * @self: a #GPasteClient instance
 * @file: the file to add
 * @error: return location for a #GError, or %NULL
 *
 * Add the file contents to the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_add_file_sync (GPasteClient *self, const gchar *file, GError **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_autofree gchar *absolute_path = NULL;

    if (!g_path_is_absolute (file))
    {
        g_autofree gchar *current_dir = g_get_current_dir ();
        absolute_path = g_build_filename (current_dir, file, NULL);
    }

    g_paste_daemon3_call_add_file_sync (G_PASTE_DAEMON3 (self), (absolute_path) ? absolute_path : file, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
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
 */
G_PASTE_VISIBLE void
g_paste_client_add_file (GPasteClient *self, const gchar *file, GAsyncReadyCallback callback, gpointer user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_autofree gchar *absolute_path = NULL;

    if (!g_path_is_absolute (file))
    {
        g_autofree gchar *current_dir = g_get_current_dir ();
        absolute_path = g_build_filename (current_dir, file, NULL);
    }

    g_paste_daemon3_call_add_file (G_PASTE_DAEMON3 (self), (absolute_path) ? absolute_path : file, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

/**
 * g_paste_client_add_file_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Add the file contents to the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_add_file_finish (GPasteClient *self,
                                GAsyncResult *result,
                                GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon3_call_add_file_finish (G_PASTE_DAEMON3 (self), result, error);
}

/**
 * g_paste_client_add_password_sync:
 * @self: a #GPasteClient instance
 * @name: the name to identify the password to add
 * @password: the password to add
 * @error: return location for a #GError, or %NULL
 *
 * Add the password to the #GPasteDaemon
 */
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
 */
/**
 * g_paste_client_add_password_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Add the password to the #GPasteDaemon
 */
G_PASTE_CLIENT_METHOD (add_password,
                       (const gchar *name, const gchar *password), (name, password))

/**
 * g_paste_client_backup_history_sync:
 * @self: a #GPasteClient instance
 * @history: the name of the history
 * @backup: the name of the backup
 * @error: return location for a #GError, or %NULL
 *
 * Backup the current history
 */
/**
 * g_paste_client_backup_history:
 * @self: a #GPasteClient instance
 * @history: the name of the history
 * @backup: the name of the backup
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Backup the current history
 */
/**
 * g_paste_client_backup_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Backup the current history
 */
G_PASTE_CLIENT_METHOD (backup_history,
                       (const gchar *history, const gchar *backup), (history, backup))

/**
 * g_paste_client_change_passphrase_sync:
 * @self: a #GPasteClient instance
 * @error: return location for a #GError, or %NULL
 *
 * Change the passphrase of the encrypted history, re-encrypting it with the new
 * one. The daemon prompts for the passphrases itself: they never travel over the
 * bus.
 */
/**
 * g_paste_client_change_passphrase:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Change the passphrase of the encrypted history, re-encrypting it with the new
 * one. The daemon prompts for the passphrases itself: they never travel over the
 * bus.
 */
/**
 * g_paste_client_change_passphrase_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Change the passphrase of the encrypted history
 */
G_PASTE_CLIENT_METHOD (change_passphrase,
                       (), ())

/**
 * g_paste_client_delete_item_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to delete
 * @error: return location for a #GError, or %NULL
 *
 * Delete an item from the #GPasteDaemon
 */
/**
 * g_paste_client_delete_item:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to delete
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Delete an item from the #GPasteDaemon
 */
/**
 * g_paste_client_delete_item_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Delete an item from the #GPasteDaemon
 */
G_PASTE_CLIENT_METHOD (delete_item,
                       (const gchar *uuid), (uuid))

/**
 * g_paste_client_delete_history_sync:
 * @self: a #GPasteClient instance
 * @name: the name of the history to delete
 * @error: return location for a #GError, or %NULL
 *
 * Delete a history
 */
/**
 * g_paste_client_delete_history:
 * @self: a #GPasteClient instance
 * @name: the name of the history to delete
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Delete a history
 */
/**
 * g_paste_client_delete_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Delete a history
 */
G_PASTE_CLIENT_METHOD (delete_history,
                       (const gchar *name), (name))

/**
 * g_paste_client_delete_password_sync:
 * @self: a #GPasteClient instance
 * @name: the name of the password to delete
 * @error: return location for a #GError, or %NULL
 *
 * Delete the password from the #GPasteDaemon
 */
/**
 * g_paste_client_delete_password:
 * @self: a #GPasteClient instance
 * @name: the name of the password to delete
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Delete the password from the #GPasteDaemon
 */
/**
 * g_paste_client_delete_password_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Delete the password from the #GPasteDaemon
 */
G_PASTE_CLIENT_METHOD (delete_password,
                       (const gchar *name), (name))

/**
 * g_paste_client_empty_history_sync:
 * @self: a #GPasteClient instance
 * @name: the name of the history to empty
 * @error: return location for a #GError, or %NULL
 *
 * Empty the history from the #GPasteDaemon
 */
/**
 * g_paste_client_empty_history:
 * @self: a #GPasteClient instance
 * @name: the name of the history to empty
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Empty the history from the #GPasteDaemon
 */
/**
 * g_paste_client_empty_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Empty the history from the #GPasteDaemon
 */
G_PASTE_CLIENT_METHOD (empty_history,
                       (const gchar *name), (name))

/**
 * g_paste_client_get_item_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the item we want to get
 * @error: return location for a #GError, or %NULL
 *
 * Get an item from the #GPasteDaemon
 *
 * Returns: (transfer full): a new #GPasteClientItem
 */
/**
 * g_paste_client_get_item:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the item we want to get
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get an item from the #GPasteDaemon
 */
/**
 * g_paste_client_get_item_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Get an item from the #GPasteDaemon
 *
 * Returns: (transfer full): a new #GPasteClientItem
 */
G_PASTE_CLIENT_METHOD_RET (get_item,
                           GPasteClientItem *, NULL,
                           g_autoptr (GVariant) item = NULL, &item, g_paste_util_get_dbus_item_result (item),
                           (const gchar *uuid), (uuid))

/**
 * g_paste_client_get_item_at_index_sync:
 * @self: a #GPasteClient instance
 * @index: the index of the item we want to get
 * @error: return location for a #GError, or %NULL
 *
 * Get an item from the #GPasteDaemon
 *
 * Returns: (transfer full): a new #GPasteClientItem
 */
/**
 * g_paste_client_get_item_at_index:
 * @self: a #GPasteClient instance
 * @index: the index of the item we want to get
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get an item from the #GPasteDaemon
 */
/**
 * g_paste_client_get_item_at_index_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Get an item from the #GPasteDaemon
 *
 * Returns: (transfer full): a new #GPasteClientItem
 */
G_PASTE_CLIENT_METHOD_RET (get_item_at_index,
                           GPasteClientItem *, NULL,
                           g_autoptr (GVariant) item = NULL, &item, g_paste_util_get_dbus_item_result (item),
                           (guint64 index), (index))

/**
 * g_paste_client_get_items_sync:
 * @self: a #GPasteClient instance
 * @uuids: (array zero-terminated=1): the uuids of the items we want to get
 * @error: return location for a #GError, or %NULL
 *
 * Get some items from the #GPasteDaemon
 *
 * Returns: (element-type GPasteClientItem) (transfer full): a newly allocated list of items
 */
/**
 * g_paste_client_get_items:
 * @self: a #GPasteClient instance
 * @uuids: (array zero-terminated=1): the uuids of the items we want to get
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get some items from the #GPasteDaemon
 */
/**
 * g_paste_client_get_items_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Get some items from the #GPasteDaemon
 *
 * Returns: (element-type GPasteClientItem) (transfer full): a newly allocated list of items
 */
/* Hand-written for @uuids, the one method parameter with a precondition of its
 * own: the generated call would build a %NULL as g_variant_new ("(^as)", NULL)
 * and crash inside g_variant_new_strv() rather than say what was wrong. Merge,
 * the other array-taking call, guards it the same way. */
G_PASTE_VISIBLE GList *
g_paste_client_get_items_sync (GPasteClient *self, const gchar * const *uuids, GError **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (uuids, NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autoptr (GVariant) items = NULL;

    if (!g_paste_daemon3_call_get_items_sync (G_PASTE_DAEMON3 (self), uuids, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, &items, NULL /* cancellable */, error))
        return NULL;

    return g_paste_util_get_dbus_items_result (items);
}

G_PASTE_VISIBLE void
g_paste_client_get_items (GPasteClient *self, const gchar * const *uuids, GAsyncReadyCallback callback, gpointer user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (uuids);

    g_paste_daemon3_call_get_items (G_PASTE_DAEMON3 (self), uuids, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

G_PASTE_VISIBLE GList *
g_paste_client_get_items_finish (GPasteClient *self,
                                 GAsyncResult *result,
                                 GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autoptr (GVariant) items = NULL;

    if (!g_paste_daemon3_call_get_items_finish (G_PASTE_DAEMON3 (self), &items, result, error))
        return NULL;

    return g_paste_util_get_dbus_items_result (items);
}

/**
 * g_paste_client_get_favourites_sync:
 * @self: a #GPasteClient instance
 * @error: return location for a #GError, or %NULL
 *
 * Get the pinned items from the #GPasteDaemon
 *
 * Returns: (element-type GPasteClientItem) (transfer full): a newly allocated list of items
 */
/**
 * g_paste_client_get_favourites:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get the pinned items from the #GPasteDaemon
 */
/**
 * g_paste_client_get_favourites_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Get the pinned items from the #GPasteDaemon
 *
 * Returns: (element-type GPasteClientItem) (transfer full): a newly allocated list of items
 */
G_PASTE_CLIENT_METHOD_RET (get_favourites,
                           GList *, NULL,
                           g_autoptr (GVariant) favourites = NULL, &favourites, g_paste_util_get_dbus_items_result (favourites),
                           (), ())

/**
 * g_paste_client_get_history_sync:
 * @self: a #GPasteClient instance
 * @error: return location for a #GError, or %NULL
 *
 * Get the history from the #GPasteDaemon
 *
 * Returns: (element-type GPasteClientItem) (transfer full): a newly allocated list of items
 */
/**
 * g_paste_client_get_history:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get the history from the #GPasteDaemon
 */
/**
 * g_paste_client_get_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Get the history from the #GPasteDaemon
 *
 * Returns: (element-type GPasteClientItem) (transfer full): a newly allocated list of items
 */
G_PASTE_CLIENT_METHOD_RET (get_history,
                           GList *, NULL,
                           g_autoptr (GVariant) history = NULL, &history, g_paste_util_get_dbus_items_result (history),
                           (), ())

/**
 * g_paste_client_get_history_size_sync:
 * @self: a #GPasteClient instance
 * @name: the name of the history
 * @error: return location for a #GError, or %NULL
 *
 * Get the history size from the #GPasteDaemon
 *
 * Returns: the size of the history
 */
/**
 * g_paste_client_get_history_size:
 * @self: a #GPasteClient instance
 * @name: the name of the history
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get the history isize from the #GPasteDaemon
 */
/**
 * g_paste_client_get_history_size_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Get the history size from the #GPasteDaemon
 *
 * Returns: the size of the history
 */
G_PASTE_CLIENT_METHOD_RET (get_history_size,
                           guint64, 0,
                           guint64 size = 0, &size, size,
                           (const gchar *name), (name))

/**
 * g_paste_client_get_image_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the image element we want to get
 * @error: return location for a #GError, or %NULL
 *
 * Get an image item's bytes from the #GPasteDaemon, so clients never have to
 * dereference the item's path themselves
 *
 * Returns: (transfer full): the PNG image bytes
 */
/**
 * g_paste_client_get_image:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the image element we want to get
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get an image item's bytes from the #GPasteDaemon
 */
/**
 * g_paste_client_get_image_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Get an image item's bytes from the #GPasteDaemon
 *
 * Returns: (transfer full): the PNG image bytes
 */
G_PASTE_CLIENT_METHOD_RET (get_image,
                           GBytes *, NULL,
                           g_autoptr (GVariant) image = NULL, &image, g_variant_get_data_as_bytes (image),
                           (const gchar *uuid), (uuid))

/**
 * g_paste_client_get_uris_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the uris item we want the uris of
 * @error: return location for a #GError, or %NULL
 *
 * Get the uris a uris item holds from the #GPasteDaemon, the item's own value
 * being the decorated string a user is shown rather than the uris themselves
 *
 * Returns: (transfer full): a newly allocated %NULL-terminated array of strings
 */
/**
 * g_paste_client_get_uris:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the uris item we want the uris of
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get the uris a uris item holds from the #GPasteDaemon
 */
/**
 * g_paste_client_get_uris_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Get the uris a uris item holds from the #GPasteDaemon
 *
 * Returns: (transfer full): a newly allocated %NULL-terminated array of strings
 */
G_PASTE_CLIENT_METHOD_RET (get_uris,
                           GStrv, NULL,
                           g_auto (GStrv) uris = NULL, &uris, g_steal_pointer (&uris),
                           (const gchar *uuid), (uuid))

/**
 * g_paste_client_list_histories_sync:
 * @self: a #GPasteClient instance
 * @error: return location for a #GError, or %NULL
 *
 * List all available histories
 *
 * Returns: (transfer full): a newly allocated %NULL-terminated array of strings
 */
/**
 * g_paste_client_list_histories:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * List all available histories
 */
/**
 * g_paste_client_list_histories_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * List all available histories
 *
 * Returns: (transfer full): a newly allocated %NULL-terminated array of strings
 */
G_PASTE_CLIENT_METHOD_RET (list_histories,
                           GStrv, NULL,
                           g_auto (GStrv) histories = NULL, &histories, g_steal_pointer (&histories),
                           (), ())

/**
 * g_paste_client_merge_sync:
 * @self: a #GPasteClient instance
 * @decoration: (nullable): the decoration to apply to each entry
 * @separator: (nullable): the separator to add between each entry
 * @uuids: (array zero-terminated=1): the uuids of the elements we want to get
 * @error: return location for a #GError, or %NULL
 *
 * Merge some history entries
 *
 * If decoration is " and separator is , and entries are foo bar baz
 * result will be "foo","bar","baz"
 */
G_PASTE_VISIBLE void
g_paste_client_merge_sync (GPasteClient *self, const gchar *decoration, const gchar *separator, const gchar * const *uuids, GError **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (uuids);
    g_return_if_fail (!error || !(*error));

    g_paste_daemon3_call_merge_sync (G_PASTE_DAEMON3 (self),
                                     (decoration) ? decoration : "",
                                     (separator) ? separator : "",
                                     uuids,
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1, /* timeout */
                                     NULL, /* cancellable */
                                     error);
}

/**
 * g_paste_client_merge:
 * @self: a #GPasteClient instance
 * @decoration: (nullable): the decoration to apply to each entry
 * @separator: (nullable): the separator to add between each entry
 * @uuids: (array zero-terminated=1): the uuids of the elements we want to get
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Merge some history entries
 *
 * If decoration is " and separator is , and entries are foo bar baz
 * result will be "foo","bar","baz"
 */
G_PASTE_VISIBLE void
g_paste_client_merge (GPasteClient *self, const gchar *decoration, const gchar *separator, const gchar * const *uuids, GAsyncReadyCallback callback, gpointer user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (uuids);

    g_paste_daemon3_call_merge (G_PASTE_DAEMON3 (self),
                                (decoration) ? decoration : "",
                                (separator) ? separator : "",
                                uuids,
                                G_DBUS_CALL_FLAGS_NONE,
                                -1, /* timeout */
                                NULL, /* cancellable */
                                callback,
                                user_data);
}

/**
 * g_paste_client_merge_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Merge some history entries
 */
G_PASTE_VISIBLE void
g_paste_client_merge_finish (GPasteClient *self,
                             GAsyncResult *result,
                             GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon3_call_merge_finish (G_PASTE_DAEMON3 (self), result, error);
}

/**
 * g_paste_client_report_extension_state_sync:
 * @self: a #GPasteClient instance
 * @state: the new state of the extension
 * @error: return location for a #GError, or %NULL
 *
 * Call this when the extension changes its state
 */
/**
 * g_paste_client_report_extension_state:
 * @self: a #GPasteClient instance
 * @state: the new state of the extension
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Call this when the extension changes its state
 */
/**
 * g_paste_client_report_extension_state_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Call this when the extension changes its state
 */
G_PASTE_CLIENT_METHOD (report_extension_state,
                       (gboolean state), (state))

/**
 * g_paste_client_reexecute_sync:
 * @self: a #GPasteClient instance
 * @error: return location for a #GError, or %NULL
 *
 * Reexecute the #GPasteDaemon
 */
/**
 * g_paste_client_reexecute:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Reexecute the #GPasteDaemon
 */
/**
 * g_paste_client_reexecute_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Reexecute the #GPasteDaemon
 */
G_PASTE_CLIENT_METHOD (reexecute,
                       (), ())

/**
 * g_paste_client_rename_password_sync:
 * @self: a #GPasteClient instance
 * @old_name: the name of the password to rename
 * @new_name: the new name to give it
 * @error: return location for a #GError, or %NULL
 *
 * Rename the password in the #GPasteDaemon
 */
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
 */
/**
 * g_paste_client_rename_password_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Rename the password in the #GPasteDaemon
 */
G_PASTE_CLIENT_METHOD (rename_password,
                       (const gchar *old_name, const gchar *new_name), (old_name, new_name))

/**
 * g_paste_client_replace_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to replace
 * @contents: the replacement contents
 * @error: return location for a #GError, or %NULL
 *
 * Replace the contents of an item
 */
/**
 * g_paste_client_replace:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to replace
 * @contents: the replacement contents
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Replace the contents of an item
 */
/**
 * g_paste_client_replace_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Replace the contents of an item
 */
G_PASTE_CLIENT_METHOD (replace,
                       (const gchar *uuid, const gchar *contents), (uuid, contents))

/**
 * g_paste_client_search_sync:
 * @self: a #GPasteClient instance
 * @pattern: the pattern to look for in history
 * @error: return location for a #GError, or %NULL
 *
 * Search for items matching @pattern in history
 *
 * Returns: (element-type GPasteClientItem) (transfer full): a newly allocated list of items
 */
/**
 * g_paste_client_search:
 * @self: a #GPasteClient instance
 * @pattern: the pattern to look for in history
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Search for items matching @pattern in history
 */
/**
 * g_paste_client_search_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Search for items matching @pattern in history
 *
 * Returns: (element-type GPasteClientItem) (transfer full): a newly allocated list of items
 */
G_PASTE_CLIENT_METHOD_RET (search,
                           GList *, NULL,
                           g_autoptr (GVariant) results = NULL, &results, g_paste_util_get_dbus_items_result (results),
                           (const gchar *pattern), (pattern))

/**
 * g_paste_client_select_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to select
 * @error: return location for a #GError, or %NULL
 *
 * Select an item from the #GPasteDaemon
 */
/**
 * g_paste_client_select:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to select
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Select an item from the #GPasteDaemon
 */
/**
 * g_paste_client_select_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Select an item from the #GPasteDaemon
 */
G_PASTE_CLIENT_METHOD (select,
                       (const gchar *uuid), (uuid))

/**
 * g_paste_client_set_favourite_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the item to pin, or to let go of
 * @favourite: whether the item should be pinned
 * @error: return location for a #GError, or %NULL
 *
 * Pin an item, exempting it from the history's size and memory caps, or let it
 * go again
 */
/**
 * g_paste_client_set_favourite:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the item to pin, or to let go of
 * @favourite: whether the item should be pinned
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Pin an item, exempting it from the history's size and memory caps, or let it
 * go again
 */
/**
 * g_paste_client_set_favourite_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Pin an item, exempting it from the history's size and memory caps, or let it
 * go again
 */
G_PASTE_CLIENT_METHOD (set_favourite,
                       (const gchar *uuid, gboolean favourite), (uuid, favourite))

/**
 * g_paste_client_set_password_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to set as password
 * @name: the name to identify the password
 * @error: return location for a #GError, or %NULL
 *
 * Set the item as password
 */
/**
 * g_paste_client_set_password:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to set as password
 * @name: the name to identify the password
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: The data to pass to @callback.
 *
 * Set the item as password
 */
/**
 * g_paste_client_set_password_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Set the item as password
 */
G_PASTE_CLIENT_METHOD (set_password,
                       (const gchar *uuid, const gchar *name), (uuid, name))

/**
 * g_paste_client_show_history_sync:
 * @self: a #GPasteClient instance
 * @error: return location for a #GError, or %NULL
 *
 * Emit the ShowHistory signal
 */
/**
 * g_paste_client_show_history:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Emit the ShowHistory signal
 */
/**
 * g_paste_client_show_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Emit the ShowHistory signal
 */
G_PASTE_CLIENT_METHOD (show_history,
                       (), ())

/**
 * g_paste_client_switch_history_sync:
 * @self: a #GPasteClient instance
 * @name: the name of the history to switch to
 * @error: return location for a #GError, or %NULL
 *
 * Switch to another history
 */
/**
 * g_paste_client_switch_history:
 * @self: a #GPasteClient instance
 * @name: the name of the history to switch to
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable):The data to pass to @callback.
 *
 * Switch to another history
 */
/**
 * g_paste_client_switch_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Switch to another history
 */
G_PASTE_CLIENT_METHOD (switch_history,
                       (const gchar *name), (name))

/**
 * g_paste_client_set_active_sync:
 * @self: a #GPasteClient instance
 * @state: the new tracking state of the #GPasteDaemon
 * @error: return location for a #GError, or %NULL
 *
 * Change the tracking state of the #GPasteDaemon
 */
/**
 * g_paste_client_set_active:
 * @self: a #GPasteClient instance
 * @state: the new tracking state of the #GPasteDaemon
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Change the tracking state of the #GPasteDaemon
 */
/**
 * g_paste_client_set_active_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Change the tracking state of the #GPasteDaemon
 */
G_PASTE_CLIENT_METHOD (set_active,
                       (gboolean state), (state))

/**
 * g_paste_client_upload_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to upload
 * @error: return location for a #GError, or %NULL
 *
 * Upload an item to a pastebin service
 */
/**
 * g_paste_client_upload:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to upload
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Upload an item to a pastebin service
 */
/**
 * g_paste_client_upload_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Upload an item to a pastebin service
 */
G_PASTE_CLIENT_METHOD (upload,
                       (const gchar *uuid), (uuid))

#undef ARGLIST

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
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), FALSE);

    /* Read the cached property rather than g_paste_daemon3_get_active(): that
     * dispatches through the interface vtable, which only the generated proxy
     * fills in, and GPasteClient implements the interface instead of deriving
     * from it. */
    g_autoptr (GVariant) active = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), G_PASTE_DAEMON_PROP_ACTIVE);

    return (active) ? g_variant_get_boolean (active) : FALSE;
}

/**
 * g_paste_client_get_history_name:
 * @self: a #GPasteClient instance
 *
 * Get the name of the history currently in use.
 *
 * This reads the cached #GPasteClient:history property rather than the bus, so
 * unlike the rest of the surface here it neither blocks nor has an async twin.
 *
 * Returns: (transfer full) (nullable): the name of the current history
 */
G_PASTE_VISIBLE gchar *
g_paste_client_get_history_name (GPasteClient *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);

    /* Same as g_paste_client_is_active(): the cached property, not the
     * interface's own getter. */
    g_autoptr (GVariant) history = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), G_PASTE_DAEMON_PROP_HISTORY);

    return (history) ? g_variant_dup_string (history, NULL) : NULL;
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
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);

    /* Same as g_paste_client_is_active(): the cached property, not the
     * interface's own getter. */
    g_autoptr (GVariant) version = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), G_PASTE_DAEMON_PROP_VERSION);

    return (version) ? g_variant_dup_string (version, NULL) : NULL;
}

static void
g_paste_client_daemon3_iface_init (GPasteDaemon3Iface *iface G_GNUC_UNUSED)
{
    /* Nothing to fill in: the interface is implemented for its client half, and
     * the generated g_paste_daemon3_call_*() go straight through GDBusProxy.
     * The handle_*() and get_*() vfuncs belong to whoever *serves* the
     * interface, which is GPasteDaemon's skeleton, not this proxy. */
}

static void
g_paste_client_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    GPasteClient *self = G_PASTE_CLIENT (object);

    switch (prop_id)
    {
    case PROP_ACTIVE:
        g_value_set_boolean (value, g_paste_client_is_active (self));
        break;
    case PROP_HISTORY:
        g_value_take_string (value, g_paste_client_get_history_name (self));
        break;
    case PROP_VERSION:
        g_value_take_string (value, g_paste_client_get_version (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

/* Every property is read-only on the wire. The interface declares them writable
 * all the same, so overriding them requires a setter to exist, but there is
 * nothing a client could set: say so rather than pretend. */
static void
g_paste_client_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value G_GNUC_UNUSED,
                             GParamSpec   *pspec)
{
    switch (prop_id)
    {
    case PROP_ACTIVE:
    case PROP_HISTORY:
    case PROP_VERSION:
        g_warning ("GPasteClient:%s is owned by the daemon and cannot be set", pspec->name);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
g_paste_client_g_signal (GDBusProxy  *proxy,
                         const gchar *sender_name G_GNUC_UNUSED,
                         const gchar *signal_name,
                         GVariant    *parameters)
{
    GPasteClient *self = G_PASTE_CLIENT (proxy);
    const gchar *history;

    if (g_paste_str_equal (signal_name, G_PASTE_DAEMON_SIG_SHOW_HISTORY))
        g_signal_emit (self, signals[SHOW_HISTORY], 0 /* detail */);
    else if (g_paste_str_equal (signal_name, G_PASTE_DAEMON_SIG_DELETE_HISTORY))
    {
        g_variant_get (parameters, "(&s)", &history);
        g_signal_emit (self, signals[DELETE_HISTORY], 0 /* detail */, history);
    }
    else if (g_paste_str_equal (signal_name, G_PASTE_DAEMON_SIG_EMPTY_HISTORY))
    {
        g_variant_get (parameters, "(&s)", &history);
        g_signal_emit (self, signals[EMPTY_HISTORY], 0 /* detail */, history);
    }
    else if (g_paste_str_equal (signal_name, G_PASTE_DAEMON_SIG_HISTORIES_CHANGED))
        g_signal_emit (self, signals[HISTORIES_CHANGED], 0 /* detail */);
    else if (g_paste_str_equal (signal_name, G_PASTE_DAEMON_SIG_UPDATE))
    {
        guint32 action, target;
        const gchar *uuid;
        guint64 index;

        g_variant_get (parameters, "(uu&st)", &action, &target, &uuid, &index);

        /* A daemon newer than us can name an action or a target we do not know —
         * which is exactly what a re-exec after an upgrade leaves us talking to,
         * with this very signal arriving in a gnome-shell that still runs the old
         * library. Skip such an update rather than hand a handler a value its
         * switch has no case for. */
        if (!g_enum_get_value (g_type_class_peek (G_PASTE_TYPE_UPDATE_ACTION), action) ||
            !g_enum_get_value (g_type_class_peek (G_PASTE_TYPE_UPDATE_TARGET), target))
        {
            g_warning ("Ignoring an update from a daemon speaking of an unknown action or target");
            return;
        }

        g_signal_emit (self, signals[UPDATE], 0 /* detail */, action, target, uuid, index);
    }
}

/* A property stops being what it was in two ways, and only one of them carries a
 * value: it changes, or it is invalidated -- which the proxy synthesizes for
 * every property it had cached the moment the daemon's name loses its owner.
 * Both mean whatever mirrors it must stop drawing what it last saw, so both
 * notify. */
static gboolean
g_paste_client_property_moved (GVariantDict        *changed,
                               const gchar * const *invalidated,
                               const gchar         *property)
{
    return g_variant_dict_contains (changed, property) || g_strv_contains (invalidated, property);
}

static void
g_paste_client_g_properties_changed (GDBusProxy          *proxy,
                                     GVariant            *changed_properties,
                                     const gchar * const *invalidated_properties)
{
    GPasteClient *self = G_PASTE_CLIENT (proxy);
    GVariantDict dict;

    g_variant_dict_init (&dict, changed_properties);

    if (g_paste_client_property_moved (&dict, invalidated_properties, G_PASTE_DAEMON_PROP_ACTIVE))
    {
        g_object_notify (G_OBJECT (self), "active");
        g_signal_emit (self, signals[TRACKING], 0 /* detail */, g_paste_client_is_active (self));
    }

    if (g_paste_client_property_moved (&dict, invalidated_properties, G_PASTE_DAEMON_PROP_HISTORY))
        g_object_notify (G_OBJECT (self), "history");

    if (g_paste_client_property_moved (&dict, invalidated_properties, G_PASTE_DAEMON_PROP_VERSION))
        g_object_notify (G_OBJECT (self), "version");

    g_variant_dict_clear (&dict);
}

/* The other half of a daemon restart. The proxy empties its property cache when
 * the name loses its owner (announced above) and fills it again with a GetAll on
 * the next owner -- that one it announces to nobody, "g-name-owner" being all it
 * emits, and only once the new values are in. So this is where the properties
 * are said to have moved: a daemon that comes back on another history, or
 * tracking where it was not, is otherwise mirrored by rows that never heard of
 * it. */
static void
g_paste_client_notify (GObject    *object,
                       GParamSpec *pspec)
{
    GObjectClass *parent_class = G_OBJECT_CLASS (g_paste_client_parent_class);

    if (g_paste_str_equal (pspec->name, "g-name-owner"))
    {
        g_autofree gchar *owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (object));

        if (owner)
        {
            GPasteClient *self = G_PASTE_CLIENT (object);

            g_object_notify (object, "active");
            g_signal_emit (self, signals[TRACKING], 0 /* detail */, g_paste_client_is_active (self));
            g_object_notify (object, "history");
            g_object_notify (object, "version");
        }
    }

    if (parent_class->notify)
        parent_class->notify (object, pspec);
}

static void
g_paste_client_class_init (GPasteClientClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GDBusProxyClass *proxy_class = G_DBUS_PROXY_CLASS (klass);

    /* Register the error domain before any call can return one. GDBus maps a
     * remote error name back to its domain only if it was registered by the
     * time the reply is decoded, and this class is the only way in to the
     * daemon, so doing it here is what makes g_error_matches (err,
     * G_PASTE_ERROR, ...) work on the very first failure rather than the second.
     * The daemon side registers through the same call when it throws. */
    g_paste_error_quark ();

    object_class->get_property = g_paste_client_get_property;
    object_class->set_property = g_paste_client_set_property;
    object_class->notify = g_paste_client_notify;

    proxy_class->g_signal = g_paste_client_g_signal;
    proxy_class->g_properties_changed = g_paste_client_g_properties_changed;

    /* Installs the interface's "Active", "History" and "Version" on us, in the
     * PROP_* order declared above. */
    g_paste_daemon3_override_properties (object_class, PROP_ACTIVE);

    /**
     * GPasteClient::delete-history:
     * @client: the object on which the signal was emitted
     * @history: the name of the history we deleted
     *
     * The "delete-history" signal is emitted when we delete
     * a history.
     */
    signals[DELETE_HISTORY] = NEW_SIGNAL_WITH_DATA ("delete-history", STRING);

    /**
     * GPasteClient::empty-history:
     * @client: the object on which the signal was emitted
     * @history: the name of the history we emptied
     *
     * The "empty-history" signal is emitted when we empty
     * a history.
     */
    signals[EMPTY_HISTORY] = NEW_SIGNAL_WITH_DATA ("empty-history", STRING);

    /**
     * GPasteClient::histories-changed:
     * @client: the object on which the signal was emitted
     *
     * The "histories-changed" signal is emitted when the set of histories
     * changed, so anything listing them should ask again. Distinct from
     * switching: a backup creates a history without making it the current one,
     * which no change of #GPasteClient:history can express.
     */
    signals[HISTORIES_CHANGED] = NEW_SIGNAL ("histories-changed");

    /**
     * GPasteClient::show-history:
     * @client: the object on which the signal was emitted
     *
     * The "show-history" signal is emitted when we switch
     * from a history to another.
     */
    signals[SHOW_HISTORY] = NEW_SIGNAL ("show-history");

    /**
     * GPasteClient::track:
     * @client: the object on which the signal was emitted
     * @tracking_state: whether we're now tracking or not
     *
     * The "tracking" signal is emitted when the daemon starts or stops tracking
     * clipboard changes.
     */
    signals[TRACKING] = NEW_SIGNAL_WITH_DATA ("tracking", BOOLEAN);

    /**
     * GPasteClient::update:
     * @client: the object on which the signal was emitted
     * @action: the kind of update
     * @target: the items which need updating
     * @uuid: the item the update is about, when the target is ITEM; "" otherwise
     * @index: where that item sits, when the target is ITEM
     *
     * The "update" signal is emitted whenever anything changed
     * in the history (something was added, removed, selected, replaced...).
     */
    signals[UPDATE] = g_signal_new ("update",
                                    G_PASTE_TYPE_CLIENT,
                                    G_SIGNAL_RUN_LAST,
                                    0, /* class offset */
                                    NULL, /* accumulator */
                                    NULL, /* accumulator data */
                                    g_cclosure_marshal_generic,
                                    G_TYPE_NONE,
                                    4, /* number of params */
                                    G_PASTE_TYPE_UPDATE_ACTION,
                                    G_PASTE_TYPE_UPDATE_TARGET,
                                    G_TYPE_STRING,
                                    G_TYPE_UINT64);
}

static void
g_paste_client_init (GPasteClient *self)
{
    /* Straight out of the generated binding, so the wire format this proxy
     * expects and the one the daemon serves cannot drift apart. */
    g_dbus_proxy_set_interface_info (G_DBUS_PROXY (self), g_paste_daemon3_interface_info ());
}

/**
 * g_paste_client_new_sync:
 * @error: return location for a #GError, or %NULL
 *
 * Create a new instance of #GPasteClient
 *
 * This only reaches the bus, never the daemon's own code, so a failure is
 * always a %G_DBUS_ERROR or a %G_IO_ERROR — never a %G_PASTE_ERROR.
 *
 * Returns: (transfer full): a newly allocated #GPasteClient
 *                           free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClient *
g_paste_client_new_sync (GError **error)
{
    GInitable *self = g_initable_new (G_PASTE_TYPE_CLIENT,
                                      NULL, /* cancellable */
                                      error,
                                      "g-bus-type",       G_BUS_TYPE_SESSION,
                                      "g-flags",          G_DBUS_PROXY_FLAGS_NONE,
                                      "g-name",           G_PASTE_BUS_NAME,
                                      "g-object-path",    G_PASTE_DAEMON_OBJECT_PATH,
                                      "g-interface-name", G_PASTE_DAEMON_INTERFACE_NAME,
                                      NULL);

    return (self) ? G_PASTE_CLIENT (self) : NULL;
}

/**
 * g_paste_client_new:
 * @callback: Callback function to invoke when the proxy is ready.
 * @user_data: User data to pass to @callback.
 *
 * Create a new instance of #GPasteClient
 */
G_PASTE_VISIBLE void
g_paste_client_new (GAsyncReadyCallback callback,
                    gpointer            user_data)
{
    g_async_initable_new_async (G_PASTE_TYPE_CLIENT,
                                G_PRIORITY_DEFAULT,
                                NULL, /* cancellable */
                                callback,
                                user_data,
                                "g-bus-type",       G_BUS_TYPE_SESSION,
                                "g-flags",          G_DBUS_PROXY_FLAGS_NONE,
                                "g-name",           G_PASTE_BUS_NAME,
                                "g-object-path",    G_PASTE_DAEMON_OBJECT_PATH,
                                "g-interface-name", G_PASTE_DAEMON_INTERFACE_NAME,
                                NULL);
}

/**
 * g_paste_client_new_finish:
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback function passed to the async ctor.
 * @error: return location for a #GError, or %NULL
 *
 * Create a new instance of #GPasteClient
 *
 * This only reaches the bus, never the daemon's own code, so a failure is
 * always a %G_DBUS_ERROR or a %G_IO_ERROR — never a %G_PASTE_ERROR.
 *
 * Returns: (transfer full): a newly allocated #GPasteClient
 *                           free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClient *
g_paste_client_new_finish (GAsyncResult *result,
                           GError      **error)
{
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autoptr (GObject) source = g_async_result_get_source_object (result);

    g_assert (source);

    GObject *self = g_async_initable_new_finish (G_ASYNC_INITABLE (source), result, error);

    return (self) ? G_PASTE_CLIENT (self) : NULL;
}
