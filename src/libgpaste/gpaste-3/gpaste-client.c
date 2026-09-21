// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-daemon3.h>
#include <gpaste-3/gpaste-error.h>
#include <gpaste-3/gpaste-gdbus-defines.h>
#include <gpaste-3/gpaste-util.h>
#include <gpaste-3/gpaste-update-enums.h>

/* How long a followed client waits on a daemon that is not there: a grace
 * second before a name nobody owns reads as missing -- what an upgrade takes to
 * put one back -- and two minutes before an owner that never serves stops
 * reading as starting -- long enough for a re-exec, an upgrade or a manual
 * restart. Past that, trying again is the user's call. */
#define G_PASTE_CLIENT_GRACE_SECONDS   1
#define G_PASTE_CLIENT_GIVE_UP_SECONDS 127

struct _GPasteClient
{
    GDBusProxy parent_instance;

    /* What was last announced of #GPasteClient:daemon-presence, which is what
     * tells a change from a repeat. */
    GPasteDaemonPresence presence;

    /* See g_paste_client_follow_daemon (). */
    gboolean             following;
    /* Whether the wait has run out with nothing served. */
    gboolean             spent;
    guint                grace_source;
    guint                give_up_source;
    /* The request for a daemon in flight, cancelled and replaced by the next:
     * the wait is the latest one's to start, an older one having been
     * overtaken. */
    GCancellable        *probe;

    /* The bus's own NameOwnerChanged, for the handoffs the proxy learns of
     * late (on_name_owner_changed ()), and the owner the last one handed the
     * name to. */
    guint                owner_watch;
    gchar               *successor;
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
static void g_paste_client_daemon3_iface_init        (GPasteDaemon3Iface  *iface);
static void g_paste_client_initable_iface_init       (GInitableIface      *iface);
static void g_paste_client_async_initable_iface_init (GAsyncInitableIface *iface);

/* GDBusProxy implements both initables already; ours wrap its own, so that
 * however a client is built the presence is read once it is (see
 * g_paste_client_seed_presence ()). Three interfaces are more than
 * G_PASTE_DEFINE_TYPE_WITH_INTERFACE takes, and a wrapper passing the
 * G_IMPLEMENT_INTERFACE () list through would expand it into bare commas. */
G_DEFINE_TYPE_WITH_CODE (GPasteClient, g_paste_client, G_TYPE_DBUS_PROXY,
                         G_IMPLEMENT_INTERFACE (G_TYPE_PASTE_DAEMON3, g_paste_client_daemon3_iface_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, g_paste_client_initable_iface_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, g_paste_client_async_initable_iface_init))

static GInitableIface      *g_paste_client_parent_initable_iface;
static GAsyncInitableIface *g_paste_client_parent_async_initable_iface;

/* The ids g_paste_daemon3_override_properties() hands out, in the order the
 * interface declares them. */
enum
{
    PROP_ACTIVE = 1,
    PROP_HISTORY,
    PROP_VERSION,
    /* Ours, after the interface's. */
    PROP_DAEMON_PRESENCE,
};

static GParamSpec *daemon_presence_pspec = NULL;

enum
{
    HISTORIES_CHANGED,
    HISTORY_DELETED,
    HISTORY_EMPTIED,
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
                           GCancellable       *cancellable,                                     \
                           GAsyncReadyCallback callback,                                        \
                           gpointer            user_data)                                       \
    {                                                                                           \
        g_return_if_fail (G_PASTE_IS_CLIENT (self));                                            \
                                                                                                \
        g_paste_daemon3_call_##name (G_PASTE_DAEMON3 (self) ARGLIST ARGS,                       \
                                     G_DBUS_CALL_FLAGS_NONE,                                    \
                                     -1, /* timeout */                                          \
                                     cancellable,                                               \
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

/* Every method but two is answered out of what the daemon already holds, so the
 * GDBusProxy default is as much time as any of them can need. Upload is one
 * exception: it answers only once the pastebin has taken the paste, a network
 * round trip a large item or a loaded service pushes well past those 25
 * seconds -- and a call that times out reports a failure the upload did not
 * have, having already succeeded on the daemon's side.
 *
 * Reexecute is the other: expiry waits for the selections to be identified.
 * Each wait has a sixty-second deadline and completion revalidation retries
 * for at most thirty seconds, so this allowance also covers the final wait.
 * The CLI preserves deadline errors; only an unsupported method uses SIGUSR1.
 */
#define G_PASTE_CLIENT_TIMEOUT_DEFAULT   -1
#define G_PASTE_CLIENT_TIMEOUT_UPLOAD    (10 * 60 * 1000)
#define G_PASTE_CLIENT_TIMEOUT_REEXECUTE (2 * 60 * 1000)

/* Same, for a method that answers something: @decl declares the out parameter,
 * @out passes it, @ret turns it into the return value and @fail is what every
 * bail-out returns. @timeout is how long the call waits for its reply. */
#define G_PASTE_CLIENT_METHOD_RET_FULL(name, timeout, type, fail, decl, out, ret, PARAMS, ARGS) \
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
                                                 timeout,                                       \
                                                 out,                                           \
                                                 NULL, /* cancellable */                        \
                                                 error))                                        \
            return fail;                                                                        \
                                                                                                \
        return ret;                                                                             \
    }                                                                                           \
    G_PASTE_VISIBLE void                                                                        \
    g_paste_client_##name (GPasteClient       *self ARGLIST PARAMS,                             \
                           GCancellable       *cancellable,                                     \
                           GAsyncReadyCallback callback,                                        \
                           gpointer            user_data)                                       \
    {                                                                                           \
        g_return_if_fail (G_PASTE_IS_CLIENT (self));                                            \
                                                                                                \
        g_paste_daemon3_call_##name (G_PASTE_DAEMON3 (self) ARGLIST ARGS,                       \
                                     G_DBUS_CALL_FLAGS_NONE,                                    \
                                     timeout,                                                   \
                                     cancellable,                                               \
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

#define G_PASTE_CLIENT_METHOD_RET(name, type, fail, decl, out, ret, PARAMS, ARGS)               \
    G_PASTE_CLIENT_METHOD_RET_FULL (name, G_PASTE_CLIENT_TIMEOUT_DEFAULT, type, fail, decl, out, ret, PARAMS, ARGS)

/**
 * g_paste_client_add_text_sync:
 * @self: a #GPasteClient instance
 * @text: the text to add
 * @error: return location for a #GError, or %NULL
 *
 * Add an item to the #GPasteDaemon
 *
 * Returns: (transfer full): the uuid of the item that was added
 */
/**
 * g_paste_client_add_text:
 * @self: a #GPasteClient instance
 * @text: the text to add
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Add an item to the #GPasteDaemon
 */
/**
 * g_paste_client_add_text_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Add an item to the #GPasteDaemon
 *
 * Returns: (transfer full): the uuid of the item that was added
 */
G_PASTE_CLIENT_METHOD_RET (add_text,
                           gchar *, NULL,
                           g_autofree gchar *uuid = NULL, &uuid, g_steal_pointer (&uuid),
                           (const gchar *text), (text))

/**
 * g_paste_client_add_file_sync:
 * @self: a #GPasteClient instance
 * @file: the file to add
 * @error: return location for a #GError, or %NULL
 *
 * Add the file contents to the #GPasteDaemon
 *
 * Returns: (transfer full): the uuid of the item that was added
 */
G_PASTE_VISIBLE gchar *
g_paste_client_add_file_sync (GPasteClient *self, const gchar *file, GError **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autofree gchar *absolute_path = NULL;

    if (!g_path_is_absolute (file))
    {
        g_autofree gchar *current_dir = g_get_current_dir ();
        absolute_path = g_build_filename (current_dir, file, NULL);
    }

    g_autofree gchar *uuid = NULL;

    if (!g_paste_daemon3_call_add_file_sync (G_PASTE_DAEMON3 (self), (absolute_path) ? absolute_path : file, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, &uuid, NULL /* cancellable */, error))
        return NULL;

    return g_steal_pointer (&uuid);
}

/**
 * g_paste_client_add_file:
 * @self: a #GPasteClient instance
 * @file: the file to add
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Add the file contents to the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_add_file (GPasteClient *self, const gchar *file, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_autofree gchar *absolute_path = NULL;

    if (!g_path_is_absolute (file))
    {
        g_autofree gchar *current_dir = g_get_current_dir ();
        absolute_path = g_build_filename (current_dir, file, NULL);
    }

    g_paste_daemon3_call_add_file (G_PASTE_DAEMON3 (self), (absolute_path) ? absolute_path : file, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, cancellable, callback, user_data);
}

/**
 * g_paste_client_add_file_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Add the file contents to the #GPasteDaemon
 *
 * Returns: (transfer full): the uuid of the item that was added
 */
G_PASTE_VISIBLE gchar *
g_paste_client_add_file_finish (GPasteClient *self,
                                GAsyncResult *result,
                                GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autofree gchar *uuid = NULL;

    if (!g_paste_daemon3_call_add_file_finish (G_PASTE_DAEMON3 (self), &uuid, result, error))
        return NULL;

    return g_steal_pointer (&uuid);
}

/**
 * g_paste_client_add_password_sync:
 * @self: a #GPasteClient instance
 * @name: the name to identify the password to add
 * @password: the password to add
 * @timeout: how long it may stay on the clipboard, in seconds, or 0 for as long
 *           as anything else
 * @error: return location for a #GError, or %NULL
 *
 * Add the password to the #GPasteDaemon
 *
 * A name already in use fails with %G_PASTE_ERROR_ALREADY_EXISTS in the
 * %G_PASTE_ERROR domain, leaving the history and clipboard untouched.
 * The nameless placeholder "******" may be shared by multiple passwords.
 *
 * @timeout is as in g_paste_client_make_password().
 *
 * Returns: (transfer full): the uuid of the item that was added
 */
/**
 * g_paste_client_add_password:
 * @self: a #GPasteClient instance
 * @name: the name to identify the password to add
 * @password: the password to add
 * @timeout: how long it may stay on the clipboard, in seconds, or 0 for as long
 *           as anything else
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Add the password to the #GPasteDaemon
 *
 * Name conflicts are handled as in g_paste_client_add_password_sync().
 */
/**
 * g_paste_client_add_password_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Add the password to the #GPasteDaemon
 *
 * Returns: (transfer full): the uuid of the item that was added
 */
G_PASTE_CLIENT_METHOD_RET (add_password,
                           gchar *, NULL,
                           g_autofree gchar *uuid = NULL, &uuid, g_steal_pointer (&uuid),
                           (const gchar *name, const gchar *password, guint timeout), (name, password, timeout))

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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Backup the current history
 */
/**
 * g_paste_client_backup_history_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Change the passphrase of the encrypted history, re-encrypting it with the new
 * one. The daemon prompts for the passphrases itself: they never travel over the
 * bus.
 */
/**
 * g_paste_client_change_passphrase_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Delete an item from the #GPasteDaemon
 */
/**
 * g_paste_client_delete_item_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Delete a history
 */
/**
 * g_paste_client_delete_history_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: the data to pass to @callback
 *
 * Delete the password from the #GPasteDaemon
 */
/**
 * g_paste_client_delete_password_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Empty the history from the #GPasteDaemon
 */
/**
 * g_paste_client_empty_history_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Get an item from the #GPasteDaemon
 */
/**
 * g_paste_client_get_item_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Get an item from the #GPasteDaemon
 */
/**
 * g_paste_client_get_item_at_index_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Get some items from the #GPasteDaemon
 */
/**
 * g_paste_client_get_items_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
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
g_paste_client_get_items (GPasteClient *self, const gchar * const *uuids, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (uuids);

    g_paste_daemon3_call_get_items (G_PASTE_DAEMON3 (self), uuids, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, cancellable, callback, user_data);
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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Get the pinned items from the #GPasteDaemon
 */
/**
 * g_paste_client_get_favourites_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Get the history from the #GPasteDaemon
 */
/**
 * g_paste_client_get_history_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
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
 * @error: return location for a #GError, or %NULL
 *
 * Get the size of the current history from the #GPasteDaemon. The sizes of the
 * other histories come from g_paste_client_list_histories_sync(), which answers
 * all of them at once.
 *
 * Returns: the size of the history
 */
/**
 * g_paste_client_get_history_size:
 * @self: a #GPasteClient instance
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Get the size of the current history from the #GPasteDaemon
 */
/**
 * g_paste_client_get_history_size_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Get the history size from the #GPasteDaemon
 *
 * Returns: the size of the history
 */
G_PASTE_CLIENT_METHOD_RET (get_history_size,
                           guint64, 0,
                           guint64 size = 0, &size, size,
                           (), ())

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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Get an image item's bytes from the #GPasteDaemon
 */
/**
 * g_paste_client_get_image_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
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
 * g_paste_client_get_password_timeout_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the password item we want the timeout of
 * @error: return location for a #GError, or %NULL
 *
 * Get how long a password item may stay on the clipboard, in seconds, 0 for one
 * that stays as long as anything else does
 *
 * A call of its own rather than a field every item travels with, as GetImage and
 * GetUris are: only a client about to change a timeout wants it, so as a field it
 * would be a number every item of every kind carried for the password's sake.
 *
 * Returns: the timeout in seconds, 0 on failure as well as for a password with
 *          none
 */
/**
 * g_paste_client_get_password_timeout:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the password item we want the timeout of
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Get how long a password item may stay on the clipboard, in seconds
 */
/**
 * g_paste_client_get_password_timeout_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Get how long a password item may stay on the clipboard, in seconds
 *
 * Returns: the timeout in seconds, 0 on failure as well as for a password with
 *          none
 */
G_PASTE_CLIENT_METHOD_RET (get_password_timeout,
                           guint, 0,
                           guint timeout = 0, &timeout, timeout,
                           (const gchar *uuid), (uuid))

/**
 * g_paste_client_get_uris_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the uris item we want the uris of
 * @error: return location for a #GError, or %NULL
 *
 * Get the uris a uris item holds from the #GPasteDaemon, as the array a client
 * acting on the files wants rather than the one newline-joined string the item's
 * own value is
 *
 * Returns: (transfer full): a newly allocated %NULL-terminated array of strings
 */
/**
 * g_paste_client_get_uris:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the uris item we want the uris of
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Get the uris a uris item holds from the #GPasteDaemon
 */
/**
 * g_paste_client_get_uris_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
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
 * List all available histories, each with how many items it holds: a view
 * drawing the sizes beside the names needs no call of its own per history.
 *
 * Returns: (element-type GPasteClientHistory) (transfer full): a newly allocated list of histories
 */
/**
 * g_paste_client_list_histories:
 * @self: a #GPasteClient instance
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * List all available histories, each with how many items it holds
 */
/**
 * g_paste_client_list_histories_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * List all available histories, each with how many items it holds
 *
 * Returns: (element-type GPasteClientHistory) (transfer full): a newly allocated list of histories
 */
G_PASTE_CLIENT_METHOD_RET (list_histories,
                           GList *, NULL,
                           g_autoptr (GVariant) histories = NULL, &histories, g_paste_util_get_dbus_histories_result (histories),
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
 *
 * Returns: (transfer full): the uuid of the merged item
 */
G_PASTE_VISIBLE gchar *
g_paste_client_merge_sync (GPasteClient *self, const gchar *decoration, const gchar *separator, const gchar * const *uuids, GError **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (uuids, NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autofree gchar *uuid = NULL;

    if (!g_paste_daemon3_call_merge_sync (G_PASTE_DAEMON3 (self),
                                          (decoration) ? decoration : "",
                                          (separator) ? separator : "",
                                          uuids,
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1, /* timeout */
                                          &uuid,
                                          NULL, /* cancellable */
                                          error))
        return NULL;

    return g_steal_pointer (&uuid);
}

/**
 * g_paste_client_merge:
 * @self: a #GPasteClient instance
 * @decoration: (nullable): the decoration to apply to each entry
 * @separator: (nullable): the separator to add between each entry
 * @uuids: (array zero-terminated=1): the uuids of the elements we want to get
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Merge some history entries
 *
 * If decoration is " and separator is , and entries are foo bar baz
 * result will be "foo","bar","baz"
 */
G_PASTE_VISIBLE void
g_paste_client_merge (GPasteClient *self, const gchar *decoration, const gchar *separator, const gchar * const *uuids, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (uuids);

    g_paste_daemon3_call_merge (G_PASTE_DAEMON3 (self),
                                (decoration) ? decoration : "",
                                (separator) ? separator : "",
                                uuids,
                                G_DBUS_CALL_FLAGS_NONE,
                                -1, /* timeout */
                                cancellable,
                                callback,
                                user_data);
}

/**
 * g_paste_client_merge_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Merge some history entries
 *
 * Returns: (transfer full): the uuid of the merged item
 */
G_PASTE_VISIBLE gchar *
g_paste_client_merge_finish (GPasteClient *self,
                             GAsyncResult *result,
                             GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autofree gchar *uuid = NULL;

    if (!g_paste_daemon3_call_merge_finish (G_PASTE_DAEMON3 (self), &uuid, result, error))
        return NULL;

    return g_steal_pointer (&uuid);
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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Call this when the extension changes its state
 */
/**
 * g_paste_client_report_extension_state_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Call this when the extension changes its state
 */
G_PASTE_CLIENT_METHOD (report_extension_state,
                       (gboolean state), (state))

/* Reexecute is the one method whose success can be the reply never coming: a
 * standalone daemon honouring it execs before it could answer, and the bus then
 * reports the call as having gone unanswered -- where an in-process host answers
 * once it has accepted the restart, and a refusal is answered as an error.
 * So that is read as success here, once, rather than by each caller -- a caller
 * finishing it the ordinary way reports a failure for every restart that
 * worked. Matched with its domain: G_DBUS_ERROR_NO_REPLY is 4, and so is
 * G_IO_ERROR_NOT_DIRECTORY, which a bare code comparison would take for a
 * successful re-exec. */
static void
g_paste_client_reexecute_propagate (GError  *err,
                                    GError **error)
{
    if (err && !g_error_matches (err, G_DBUS_ERROR, G_DBUS_ERROR_NO_REPLY))
        g_propagate_error (error, err);
    else
        g_clear_error (&err);
}

/**
 * g_paste_client_reexecute_sync:
 * @self: a #GPasteClient instance
 * @error: return location for a #GError, or %NULL
 *
 * Reexecute the #GPasteDaemon
 *
 * A standalone daemon that re-executes goes away without answering, so a call
 * left unanswered that way is a success and leaves @error unset.
 */
G_PASTE_VISIBLE void
g_paste_client_reexecute_sync (GPasteClient *self,
                               GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    GError *err = NULL;

    g_paste_daemon3_call_reexecute_sync (G_PASTE_DAEMON3 (self),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         G_PASTE_CLIENT_TIMEOUT_REEXECUTE,
                                         NULL, /* cancellable */
                                         &err);
    g_paste_client_reexecute_propagate (err, error);
}

/**
 * g_paste_client_reexecute:
 * @self: a #GPasteClient instance
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Reexecute the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_reexecute (GPasteClient       *self,
                          GCancellable       *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon3_call_reexecute (G_PASTE_DAEMON3 (self),
                                    G_DBUS_CALL_FLAGS_NONE,
                                    G_PASTE_CLIENT_TIMEOUT_REEXECUTE,
                                    cancellable,
                                    callback,
                                    user_data);
}

/**
 * g_paste_client_reexecute_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Reexecute the #GPasteDaemon
 *
 * As in g_paste_client_reexecute_sync(), a call the daemon left unanswered by
 * going away is a success and leaves @error unset.
 */
G_PASTE_VISIBLE void
g_paste_client_reexecute_finish (GPasteClient *self,
                                 GAsyncResult *result,
                                 GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    GError *err = NULL;

    g_paste_daemon3_call_reexecute_finish (G_PASTE_DAEMON3 (self), result, &err);
    g_paste_client_reexecute_propagate (err, error);
}

/**
 * g_paste_client_replace_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to replace
 * @contents: the replacement contents
 * @error: return location for a #GError, or %NULL
 *
 * Replace the contents of an item
 *
 * Returns: (transfer full): the uuid of the item that replaced it, which is one
 *          of its own
 */
/**
 * g_paste_client_replace:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to replace
 * @contents: the replacement contents
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: the data to pass to @callback
 *
 * Replace the contents of an item
 */
/**
 * g_paste_client_replace_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Replace the contents of an item
 *
 * Returns: (transfer full): the uuid of the item that replaced it, which is one
 *          of its own
 */
G_PASTE_CLIENT_METHOD_RET (replace,
                           gchar *, NULL,
                           g_autofree gchar *new_uuid = NULL, &new_uuid, g_steal_pointer (&new_uuid),
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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Search for items matching @pattern in history
 */
/**
 * g_paste_client_search_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Select an item from the #GPasteDaemon
 */
/**
 * g_paste_client_select_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Pin an item, exempting it from the history's size and memory caps, or let it
 * go again
 */
/**
 * g_paste_client_set_favourite_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Pin an item, exempting it from the history's size and memory caps, or let it
 * go again
 */
G_PASTE_CLIENT_METHOD (set_favourite,
                       (const gchar *uuid, gboolean favourite), (uuid, favourite))

/**
 * g_paste_client_make_password_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the text item to turn into a password, or of the password
 *        item to update
 * @name: the name to identify the password
 * @timeout: how long it may stay on the clipboard, in seconds, or 0 for as long
 *           as anything else
 * @error: return location for a #GError, or %NULL
 *
 * Turn a text item into a password item, or write a password's name and timeout
 *
 * @timeout is how long the password may stay on the clipboard once it is the
 * active item: when it runs out the daemon selects the next non-password item,
 * or clears the selection if the history holds none.
 *
 * A text item is replaced by a password item, which is an item of its own, so
 * what comes back is a new uuid; a password item is updated where it stands, so
 * what comes back is the uuid that went in.
 *
 * Returns: (transfer full): the uuid of the item this left in the history
 */
/**
 * g_paste_client_make_password:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the text item to turn into a password, or of the password
 *        item to update
 * @name: the name to identify the password
 * @timeout: how long it may stay on the clipboard, in seconds, or 0 for as long
 *           as anything else
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: the data to pass to @callback
 *
 * Turn a text item into a password item, or write a password's name and timeout
 */
/**
 * g_paste_client_make_password_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Turn a text item into a password item, or write a password's name and timeout
 *
 * Returns: (transfer full): the uuid of the item this left in the history
 */
G_PASTE_CLIENT_METHOD_RET (make_password,
                           gchar *, NULL,
                           g_autofree gchar *new_uuid = NULL, &new_uuid, g_steal_pointer (&new_uuid),
                           (const gchar *uuid, const gchar *name, guint timeout), (uuid, name, timeout))

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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Emit the ShowHistory signal
 */
/**
 * g_paste_client_show_history_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Emit the ShowHistory signal
 */
G_PASTE_CLIENT_METHOD (show_history,
                       (), ())

/**
 * g_paste_client_strip_rich_text_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the text item to strip
 * @error: return location for a #GError, or %NULL
 *
 * Drop the rich text flavours of an item, keeping the plain text it shows
 */
/**
 * g_paste_client_strip_rich_text:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the text item to strip
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Drop the rich text flavours of an item, keeping the plain text it shows
 */
/**
 * g_paste_client_strip_rich_text_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Drop the rich text flavours of an item, keeping the plain text it shows
 */
G_PASTE_CLIENT_METHOD (strip_rich_text,
                       (const gchar *uuid), (uuid))

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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Switch to another history
 */
/**
 * g_paste_client_switch_history_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
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
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Change the tracking state of the #GPasteDaemon
 */
/**
 * g_paste_client_set_active_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
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
 * Upload an item to a pastebin service, and answer where it landed. The reply
 * comes when the upload has finished, and an upload that failed fails the call.
 * An empty @uuid uploads the current item.
 *
 * Returns: (transfer full): the url the item was uploaded to
 */
/**
 * g_paste_client_upload:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to upload
 * @cancellable: (nullable): a #GCancellable to abandon the call with
 * @callback: (nullable): a #GAsyncReadyCallback to call once the request is
 *            satisfied, or %NULL to ignore the result
 * @user_data: (nullable): the data to pass to @callback
 *
 * Upload an item to a pastebin service, and answer where it landed. The reply
 * comes when the upload has finished, and an upload that failed fails the call.
 * An empty @uuid uploads the current item.
 */
/**
 * g_paste_client_upload_finish:
 * @self: a #GPasteClient instance
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Upload an item to a pastebin service, and answer where it landed. The reply
 * comes when the upload has finished, and an upload that failed fails the call.
 * An empty @uuid uploads the current item.
 *
 * Returns: (transfer full): the url the item was uploaded to
 */
G_PASTE_CLIENT_METHOD_RET_FULL (upload, G_PASTE_CLIENT_TIMEOUT_UPLOAD,
                                gchar *, NULL,
                                g_autofree gchar *url = NULL, &url, g_steal_pointer (&url),
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
 * Returns: (transfer full) (nullable): the version of the daemon
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

G_PASTE_VISIBLE GType
g_paste_daemon_presence_get_type (void)
{
    static GType etype = 0;
    if (!etype)
    {
        static const GEnumValue values[] = {
            { G_PASTE_DAEMON_PRESENCE_ABSENT,   "G_PASTE_DAEMON_PRESENCE_ABSENT",   "absent"   },
            { G_PASTE_DAEMON_PRESENCE_STARTING, "G_PASTE_DAEMON_PRESENCE_STARTING", "starting" },
            { G_PASTE_DAEMON_PRESENCE_READY,    "G_PASTE_DAEMON_PRESENCE_READY",    "ready"    },
            { 0,                                NULL,                               NULL       }
        };
        etype = g_enum_register_static (g_intern_static_string ("GPasteDaemonPresence"), values);
        g_type_class_ref (etype);
    }
    return etype;
}

/* Whether the bus has handed the name straight to another owner that the
 * proxy does not address yet (on_name_owner_changed ()). */
static gboolean
g_paste_client_handing_off (GPasteClient *self)
{
    if (!self->successor)
        return FALSE;

    g_autofree gchar *owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (self));

    return !g_paste_str_equal (owner, self->successor);
}

/* Which history is in use is the daemon's own property, cached off the proxy,
 * and it reads back %NULL while there is no daemon: whether it has a value is
 * therefore the honest answer to "is there one to ask".
 *
 * The bus name is not that answer. Both daemons own the name *before* building
 * the object that serves it, so the proxy's GetAll on a new owner finds nothing
 * at that path and leaves the cache empty; what fills it is the daemon's own
 * PropertiesChanged a moment later. A name owner with no history is therefore a
 * daemon starting -- and a migration or passphrase dialog can hold it there for
 * as long as the user takes to answer. */
static GPasteDaemonPresence
g_paste_client_compute_presence (GPasteClient *self)
{
    /* Mid-handoff, the history cached is the old daemon's
     * (on_name_owner_changed ()). */
    if (!g_paste_client_handing_off (self))
    {
        g_autofree gchar *history = g_paste_client_get_history_name (self);

        if (history)
            return G_PASTE_DAEMON_PRESENCE_READY;
    }

    /* One that owns the name and never served a history is not starting any
     * more once the wait has run out on it. */
    if (self->spent)
        return G_PASTE_DAEMON_PRESENCE_ABSENT;

    /* A probe out is a daemon asked for, and one gone for less than the grace
     * second is not yet one worth calling missing. */
    if (self->probe || self->grace_source)
        return G_PASTE_DAEMON_PRESENCE_STARTING;

    g_autofree gchar *owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (self));

    return (owner) ? G_PASTE_DAEMON_PRESENCE_STARTING : G_PASTE_DAEMON_PRESENCE_ABSENT;
}

static void
g_paste_client_update_presence (GPasteClient *self)
{
    GPasteDaemonPresence presence = g_paste_client_compute_presence (self);

    if (presence == self->presence)
        return;

    self->presence = presence;
    g_object_notify_by_pspec (G_OBJECT (self), daemon_presence_pspec);
}

/**
 * g_paste_client_get_daemon_presence:
 * @self: a #GPasteClient instance
 *
 * Get whether there is a daemon to talk to
 *
 * Returns: the #GPasteClient:daemon-presence
 */
G_PASTE_VISIBLE GPasteDaemonPresence
g_paste_client_get_daemon_presence (GPasteClient *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), G_PASTE_DAEMON_PRESENCE_ABSENT);

    return self->presence;
}

/* Neither timer asks anything of the bus. A call without an owner is what
 * would start a daemon behind the user's back, and one with an owner has
 * nothing to learn: a daemon owning the name is starting already, and what says
 * it is ready is its history turning up, never a reply. So each only moves the
 * presence along. */
static gboolean
on_grace_over (gpointer user_data)
{
    g_autoptr (GPasteClient) self = g_weak_ref_get (user_data);

    if (self)
    {
        self->grace_source = 0;
        g_paste_client_update_presence (self);
    }

    return G_SOURCE_REMOVE;
}

static gboolean
on_give_up (gpointer user_data)
{
    g_autoptr (GPasteClient) self = g_weak_ref_get (user_data);

    if (self)
    {
        self->give_up_source = 0;
        self->spent = TRUE;
        g_paste_client_update_presence (self);
    }

    return G_SOURCE_REMOVE;
}

/* Called off, whatever it was waiting on: a request out included. */
static void
g_paste_client_stop_waiting (GPasteClient *self)
{
    g_clear_handle_id (&self->grace_source, g_source_remove);
    g_clear_handle_id (&self->give_up_source, g_source_remove);
    g_paste_clear_cancellable (&self->probe);
    self->spent = FALSE;
}

/* Waited for from the start. The caller announces what that did to the
 * presence. */
static void
g_paste_client_start_waiting (GPasteClient *self)
{
    g_clear_handle_id (&self->grace_source, g_source_remove);
    g_clear_handle_id (&self->give_up_source, g_source_remove);
    self->spent = FALSE;

    self->grace_source = g_timeout_add_seconds_full (G_PRIORITY_DEFAULT, G_PASTE_CLIENT_GRACE_SECONDS, on_grace_over,
                                                     g_paste_weak_ref_new (self), g_paste_weak_ref_free);
    g_source_set_name_by_id (self->grace_source, "[GPaste] daemon grace");
    self->give_up_source = g_timeout_add_seconds_full (G_PRIORITY_DEFAULT, G_PASTE_CLIENT_GIVE_UP_SECONDS, on_give_up,
                                                       g_paste_weak_ref_new (self), g_paste_weak_ref_free);
    g_source_set_name_by_id (self->give_up_source, "[GPaste] daemon give up");
}

/* Tracked in @self->probe so that dispose () can cancel it, so it holds @self
 * weakly: see AGENTS.md. That also rules out the proxy's own call, whose task
 * would take @self as its source object. */
typedef struct
{
    GWeakRef      self;
    GCancellable *cancellable;
} ProbeData;

static void
probe_data_free (ProbeData *data)
{
    g_weak_ref_clear (&data->self);
    g_object_unref (data->cancellable);
    g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ProbeData, probe_data_free)

/* The reply is not the answer, and a failure is not one either: a daemon that
 * has just been activated owns the name before it exports anything, so the call
 * that started it is as likely as not to come back "object does not exist".
 * What says it worked is the history turning up, which stops the wait and
 * cancels this probe with it. */
static void
on_probe_ready (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
    g_autoptr (ProbeData) data = user_data;
    g_autoptr (GError) error = NULL;
    g_autoptr (GVariant) reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object), res, &error);

    g_autoptr (GPasteClient) self = g_weak_ref_get (&data->self);

    /* A cancel does not unqueue a reply already on its way, so it is asked of
     * the cancellable this probe went out on. */
    if (!self || g_cancellable_is_cancelled (data->cancellable))
        return;

    g_clear_object (&self->probe);
    g_paste_client_start_waiting (self);
    g_paste_client_update_presence (self);
}

/* The daemon is bus-activatable, so an ordinary method call is what brings it
 * back -- which is why only the user gets to make this one (see
 * on_grace_over ()). */
static void
g_paste_client_probe_daemon (GPasteClient *self)
{
    g_paste_clear_cancellable (&self->probe);
    self->probe = g_cancellable_new ();

    ProbeData *data = g_new (ProbeData, 1);
    GDBusProxy *proxy = G_DBUS_PROXY (self);

    g_weak_ref_init (&data->self, self);
    data->cancellable = g_object_ref (self->probe);

    g_dbus_connection_call (g_dbus_proxy_get_connection (proxy),
                            g_dbus_proxy_get_name (proxy),
                            g_dbus_proxy_get_object_path (proxy),
                            g_dbus_proxy_get_interface_name (proxy),
                            "GetHistorySize",
                            NULL, /* parameters */
                            NULL, /* reply type */
                            G_DBUS_CALL_FLAGS_NONE,
                            -1, /* timeout */
                            self->probe,
                            on_probe_ready,
                            data);
    g_paste_client_update_presence (self);
}

/* Both edges of the connection come through here: the proxy invalidates every
 * cached property the moment the name loses its owner, and fills them again
 * once there is a daemon to fill them from. */
static void
g_paste_client_sync_presence (GPasteClient *self)
{
    gboolean ready = g_paste_client_compute_presence (self) == G_PASTE_DAEMON_PRESENCE_READY;

    if (self->following && ready != (self->presence == G_PASTE_DAEMON_PRESENCE_READY))
    {
        g_paste_client_stop_waiting (self);

        if (!ready)
            g_paste_client_start_waiting (self);
    }

    g_paste_client_update_presence (self);
}

/**
 * g_paste_client_follow_daemon:
 * @self: a #GPasteClient instance
 * @activate: whether to ask for a daemon now, starting one if there is none
 *
 * Keep looking for the daemon whenever there is none
 *
 * A daemon going away -- an upgrade, a re-exec, a crash -- is waited for
 * without calling it, so that none is ever started behind the user's back: one
 * coming back is found through its history turning up. What that says, and
 * when the wait gives up, is #GPasteClient:daemon-presence;
 * g_paste_client_retry_daemon() starts it over.
 *
 * With @activate, a client that has no daemon now asks for one straight away,
 * which starts it: for a caller the user has just opened, whose own calls would
 * be starting one anyway. Without it, the wait starts from its beginning.
 */
G_PASTE_VISIBLE void
g_paste_client_follow_daemon (GPasteClient *self,
                              gboolean      activate)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    self->following = TRUE;

    if (self->presence == G_PASTE_DAEMON_PRESENCE_READY)
        return;

    g_paste_client_stop_waiting (self);

    if (activate)
        g_paste_client_probe_daemon (self);
    else
    {
        g_paste_client_start_waiting (self);
        g_paste_client_update_presence (self);
    }
}

/**
 * g_paste_client_unfollow_daemon:
 * @self: a #GPasteClient instance
 *
 * Stop looking for the daemon, for a caller going away while something else
 * may still hold @self
 */
G_PASTE_VISIBLE void
g_paste_client_unfollow_daemon (GPasteClient *self)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    self->following = FALSE;
    g_paste_client_stop_waiting (self);
    g_paste_client_update_presence (self);
}

/**
 * g_paste_client_retry_daemon:
 * @self: a #GPasteClient instance
 *
 * Ask for a daemon now, starting one if there is none, and wait for it from
 * the start again -- for the user asking, which is the one case where
 * starting a daemon is not going behind their back
 */
G_PASTE_VISIBLE void
g_paste_client_retry_daemon (GPasteClient *self)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_client_follow_daemon (self, TRUE);
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
    case PROP_DAEMON_PRESENCE:
        g_value_set_enum (value, self->presence);
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

/* The daemon's signals are forwarded as they come, whatever the presence.
 *
 * Mid-handoff the proxy still addresses the daemon standing down for the calls
 * it makes, which is why a consumer asks the presence before one
 * (GPasteClient:daemon-presence). A signal needs no such gate: it is matched on
 * the well-known name, and a daemon that has lost the name no longer delivers
 * any -- the bus drops them (checked on dbus-daemon; dbus-broker is assumed to
 * match), and GDBus drops one whose sender is not the owner it last saw in a
 * NameOwnerChanged (schedule_callbacks () in gdbusconnection.c), which holds
 * for any bus. One sent before the name was lost arrives before the
 * NameOwnerChanged that starts the handoff, or not at all.
 *
 * So the gates a consumer puts on the signals it follows are a rule kept
 * rather than a case the bus lets through, and no test can send one past
 * them: /ui/daemon-presence/listing-handoff emits them on the client itself.
 * Dropping the signals here instead would leave the consumers' calls
 * ungated -- a menu opening, a switcher refreshing -- and change what a
 * third-party client sees of a library it links. */
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
    else if (g_paste_str_equal (signal_name, G_PASTE_DAEMON_SIG_HISTORY_DELETED))
    {
        g_variant_get (parameters, "(&s)", &history);
        g_signal_emit (self, signals[HISTORY_DELETED], 0 /* detail */, history);
    }
    else if (g_paste_str_equal (signal_name, G_PASTE_DAEMON_SIG_HISTORY_EMPTIED))
    {
        g_variant_get (parameters, "(&s)", &history);
        g_signal_emit (self, signals[HISTORY_EMPTIED], 0 /* detail */, history);
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
 * the next owner, announcing what that found as a change -- but one that
 * answers nothing, a daemon that has not exported its object yet, leaves the
 * cache emptied without a word, "g-name-owner" being all it emits. So this is
 * where the properties are said to have moved as well: a daemon that comes back
 * on another history, or tracking where it was not, is otherwise mirrored by
 * rows that never heard of it. */
static void
g_paste_client_notify (GObject    *object,
                       GParamSpec *pspec)
{
    GObjectClass *parent_class = G_OBJECT_CLASS (g_paste_client_parent_class);
    GPasteClient *self = G_PASTE_CLIENT (object);

    if (g_paste_str_equal (pspec->name, "g-name-owner"))
    {
        g_autofree gchar *owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (object));

        if (owner)
        {
            g_object_notify (object, "active");
            g_signal_emit (self, signals[TRACKING], 0 /* detail */, g_paste_client_is_active (self));
            g_object_notify (object, "history");
            g_object_notify (object, "version");
        }

        /* A new owner is a daemon started since the last one went, which gets
         * a wait of its own rather than what is left of its predecessor's --
         * two minutes to serve, whether it took the name partway through that
         * wait or after it ran out. */
        if (owner && self->following && self->presence != G_PASTE_DAEMON_PRESENCE_READY)
            g_paste_client_start_waiting (self);

        /* An owner coming or going is a daemon starting or not, whatever the
         * history says. */
        g_paste_client_update_presence (self);
    }
    /* Ahead of the handlers of "notify::history" themselves, this being the
     * class handler: one that reads the presence finds it already moved. */
    else if (g_paste_str_equal (pspec->name, "history"))
        g_paste_client_sync_presence (self);

    if (parent_class->notify)
        parent_class->notify (object, pspec);
}

/* GDBusProxy follows the name to a new owner only once its GetAll there has
 * answered, and until then its calls still go to the old one. When the name
 * goes straight from one owner to another -- `gpaste-daemon --replace`, the
 * Shell's own daemon taking over or handing back -- that is a daemon standing
 * down, and whatever a consumer asks it in the meantime fails or answers for
 * the wrong daemon. The bus says so first, with this signal: the presence
 * leaves ready on it, and comes back once the proxy addresses the successor
 * and has read its history. Every consumer thus lets go of the old daemon on
 * the presence edge it already follows, cancelling what it had out before the
 * old daemon's answers can land, and lists the new one when it is ready
 * (/client/presence/handoff). */
static void
on_name_owner_changed (GDBusConnection *connection     G_GNUC_UNUSED,
                       const gchar     *sender_name    G_GNUC_UNUSED,
                       const gchar     *object_path    G_GNUC_UNUSED,
                       const gchar     *interface_name G_GNUC_UNUSED,
                       const gchar     *signal_name    G_GNUC_UNUSED,
                       GVariant        *parameters,
                       gpointer         user_data)
{
    g_autoptr (GPasteClient) self = g_weak_ref_get (user_data);

    if (!self)
        return;

    const gchar *old_owner;
    const gchar *new_owner;

    g_variant_get (parameters, "(&s&s&s)", NULL, &old_owner, &new_owner);

    /* An owner the proxy already addresses is one its init found, the signal
     * having been on its way meanwhile: nothing left to follow. */
    g_autofree gchar *addressed = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (self));

    if (*old_owner && *new_owner && !g_paste_str_equal (new_owner, addressed))
        g_set_str (&self->successor, new_owner);
    else
        g_clear_pointer (&self->successor, g_free);

    g_paste_client_sync_presence (self);
}

/**
 * g_paste_client_is_daemon_gone_error:
 * @error: an error a call on a #GPasteClient failed with
 *
 * Whether @error says the daemon the call went to left the bus rather than
 * refused it: it disconnected without answering, or the name had no owner by
 * the time the call reached the bus. Such a call says nothing about the
 * history, and #GPasteClient:daemon-presence announces what comes next, so a
 * caller reporting failures can leave this one out. A bus answers NoReply for
 * a recipient gone; it would also for a reply timing out on the bus, which
 * neither dbus-broker nor dbus-daemon's default configuration does, a slow
 * daemon failing on the caller's own timeout instead.
 *
 * An unknown method is not one of these, whatever the daemon: a daemon handing
 * the name over moves the presence before any call it fails can be answered
 * (see #GPasteClient:daemon-presence), and an unknown method otherwise is an
 * older daemon that lacks it.
 *
 * Returns: whether @error is a daemon having gone
 */
G_PASTE_VISIBLE gboolean
g_paste_client_is_daemon_gone_error (const GError *error)
{
    return g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_NO_REPLY) ||
           g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
           g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER);
}

static void
g_paste_client_dispose (GObject *object)
{
    GPasteClient *self = G_PASTE_CLIENT (object);

    g_paste_client_stop_waiting (self);

    if (self->owner_watch)
    {
        g_dbus_connection_signal_unsubscribe (g_dbus_proxy_get_connection (G_DBUS_PROXY (self)), self->owner_watch);
        self->owner_watch = 0;
    }

    G_OBJECT_CLASS (g_paste_client_parent_class)->dispose (object);
}

static void
g_paste_client_finalize (GObject *object)
{
    GPasteClient *self = G_PASTE_CLIENT (object);

    g_free (self->successor);

    G_OBJECT_CLASS (g_paste_client_parent_class)->finalize (object);
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

    object_class->dispose = g_paste_client_dispose;
    object_class->finalize = g_paste_client_finalize;
    object_class->get_property = g_paste_client_get_property;
    object_class->set_property = g_paste_client_set_property;
    object_class->notify = g_paste_client_notify;

    proxy_class->g_signal = g_paste_client_g_signal;
    proxy_class->g_properties_changed = g_paste_client_g_properties_changed;

    /* Installs the interface's "Active", "History" and "Version" on us, in the
     * PROP_* order declared above. */
    g_paste_daemon3_override_properties (object_class, PROP_ACTIVE);

    /**
     * GPasteClient:daemon-presence:
     *
     * Whether there is a daemon to talk to. %G_PASTE_DAEMON_PRESENCE_READY
     * once one serves a history; %G_PASTE_DAEMON_PRESENCE_STARTING while one
     * owns the bus name without serving anything yet, or while a client that
     * follows it (g_paste_client_follow_daemon()) has asked for one, or saw
     * one go within the last second or so; %G_PASTE_DAEMON_PRESENCE_ABSENT
     * otherwise.
     *
     * A client that follows the daemon reports absent about a second after a
     * daemon leaves nobody owning the name, and two minutes into an owner that
     * never serves a history, each new owner getting two minutes of its own.
     * One that does not follow has neither timer: absent the moment nobody
     * owns the name, and starting for as long as an owner serves nothing.
     * Either kind turns ready by itself when a daemon comes back.
     *
     * A daemon taking the name over straight from another is no exception: the
     * presence leaves ready the moment the bus hands the name over, and comes
     * back once the new daemon serves its history. Whatever was read from the
     * daemon before -- its histories, its items, its version -- is the old
     * one's, and anything still out to it is best given up on that edge.
     *
     * Calls made while it is not ready start a daemon: the bus activates one on
     * the first call to reach it. A caller that must not do that behind the
     * user's back holds off until it is.
     */
    /* Installed on its own rather than through an array, the ids before it
     * being the interface's. */
    daemon_presence_pspec = g_param_spec_enum ("daemon-presence", NULL, NULL,
                                               G_PASTE_TYPE_DAEMON_PRESENCE,
                                               G_PASTE_DAEMON_PRESENCE_ABSENT,
                                               G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
    g_object_class_install_property (object_class, PROP_DAEMON_PRESENCE, daemon_presence_pspec);

    /**
     * GPasteClient::history-deleted:
     * @client: the object on which the signal was emitted
     * @history: the name of the history that was deleted
     *
     * The "history-deleted" signal is emitted when a history is deleted.
     *
     * Named for what happened rather than for the method that did it, the way
     * the wire signal it carries is: a listing that only cares that the set
     * changed has #GPasteClient::histories-changed, which this is always
     * accompanied by.
     */
    signals[HISTORY_DELETED] = NEW_SIGNAL_WITH_DATA ("history-deleted", STRING);

    /**
     * GPasteClient::history-emptied:
     * @client: the object on which the signal was emitted
     * @history: the name of the history that was emptied
     *
     * The "history-emptied" signal is emitted when a history is emptied.
     */
    signals[HISTORY_EMPTIED] = NEW_SIGNAL_WITH_DATA ("history-emptied", STRING);

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

/* What the presence starts as, read once initialising the proxy has settled
 * its name owner and filled its cache, and announced like any other change: a
 * client built with g_object_new () and initialised after may have a handler
 * watching it already.
 *
 * For a daemon that serves, this announces nothing: GDBusProxy's init, the
 * synchronous and the asynchronous alike, emits g-properties-changed with the
 * GetAll reply, which notifies the history and moves the presence to ready
 * first. An owner that serves nothing yet fills no history, though, and only
 * this says it is starting (/client/presence/initables). */
static void
g_paste_client_seed_presence (GPasteClient *self)
{
    /* Only now: the connection is the init's to find. A handoff landing while
     * the init was out is one the proxy follows all the same, only without the
     * presence leaving ready for it. The name is the proxy's own, which a
     * client built through the initables chooses (/client/presence/handoff-other-name). */
    if (!self->owner_watch)
    {
        self->owner_watch = g_dbus_connection_signal_subscribe (g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                                                                "org.freedesktop.DBus",
                                                                "org.freedesktop.DBus",
                                                                "NameOwnerChanged",
                                                                "/org/freedesktop/DBus",
                                                                g_dbus_proxy_get_name (G_DBUS_PROXY (self)),
                                                                G_DBUS_SIGNAL_FLAGS_NONE,
                                                                on_name_owner_changed,
                                                                g_paste_weak_ref_new (self),
                                                                g_paste_weak_ref_free);
    }

    g_paste_client_update_presence (self);
}

static gboolean
g_paste_client_initable_init (GInitable    *initable,
                              GCancellable *cancellable,
                              GError      **error)
{
    if (!g_paste_client_parent_initable_iface->init (initable, cancellable, error))
        return FALSE;

    g_paste_client_seed_presence (G_PASTE_CLIENT (initable));

    return TRUE;
}

static void
g_paste_client_initable_iface_init (GInitableIface *iface)
{
    g_paste_client_parent_initable_iface = g_type_interface_peek_parent (iface);
    iface->init = g_paste_client_initable_init;
}

/* Only the finish: the parent's init_async, which the vtable inherits, is what
 * does the work, and this is where its outcome is known. */
static gboolean
g_paste_client_async_initable_init_finish (GAsyncInitable *initable,
                                           GAsyncResult   *res,
                                           GError        **error)
{
    if (!g_paste_client_parent_async_initable_iface->init_finish (initable, res, error))
        return FALSE;

    g_paste_client_seed_presence (G_PASTE_CLIENT (initable));

    return TRUE;
}

static void
g_paste_client_async_initable_iface_init (GAsyncInitableIface *iface)
{
    g_paste_client_parent_async_initable_iface = g_type_interface_peek_parent (iface);
    iface->init_finish = g_paste_client_async_initable_init_finish;
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
 * @user_data: the data to pass to @callback
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
 * @result: the #GAsyncResult handed to the callback
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
