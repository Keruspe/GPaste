// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-daemon2.h>
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
static void g_paste_client_daemon2_iface_init (GPasteDaemon2Iface *iface);

G_PASTE_DEFINE_TYPE_WITH_INTERFACE (Client, client, G_TYPE_DBUS_PROXY, G_TYPE_PASTE_DAEMON2, g_paste_client_daemon2_iface_init)

/* The ids g_paste_daemon2_override_properties() hands out, in the order the
 * interface declares them. */
enum
{
    PROP_ACTIVE = 1,
    PROP_VERSION,
};

enum
{
    DELETE_HISTORY,
    EMPTY_HISTORY,
    SHOW_HISTORY,
    SWITCH_HISTORY,
    TRACKING,
    UPDATE,

    LAST_SIGNAL
};

static guint64 signals[LAST_SIGNAL] = { 0 };

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

/*
 * Our own API takes an explicit length; the generated marshalling wants the
 * NULL-terminated array a D-Bus "as" is built from. Only the array is ours, so
 * g_autofree (not g_auto (GStrv)) is what frees it: the strings belong to the
 * caller.
 */
static const gchar **
g_paste_client_terminate_strv (const gchar **strv,
                               guint64       length)
{
    const gchar **terminated = g_new0 (const gchar *, length + 1);

    for (guint64 i = 0; i < length; ++i)
        terminated[i] = strv[i];

    return terminated;
}

/******************/
/* Methods / Sync */
/******************/

/**
 * g_paste_client_about_sync:
 * @self: a #GPasteClient instance
 * @error: return location for a #GError, or %NULL
 *
 * Display the about dialog
 */
G_PASTE_VISIBLE void
g_paste_client_about_sync (GPasteClient *self,
                           GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_about_sync (G_PASTE_DAEMON2 (self), G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
}

/**
 * g_paste_client_add_sync:
 * @self: a #GPasteClient instance
 * @text: the text to add
 * @error: return location for a #GError, or %NULL
 *
 * Add an item to the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_add_sync (GPasteClient *self,
                         const gchar  *text,
                         GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_add_sync (G_PASTE_DAEMON2 (self), text, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
}

/**
 * g_paste_client_add_file_sync:
 * @self: a #GPasteClient instance
 * @file: the file to add
 * @error: return location for a #GError, or %NULL
 *
 * Add the file contents to the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_add_file_sync (GPasteClient *self,
                              const gchar  *file,
                              GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_autofree gchar *absolute_path = NULL;

    if (!g_path_is_absolute (file))
    {
        g_autofree gchar *current_dir = g_get_current_dir ();
        absolute_path = g_build_filename (current_dir, file, NULL);
    }

    g_paste_daemon2_call_add_file_sync (G_PASTE_DAEMON2 (self), (absolute_path) ? absolute_path : file, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
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
G_PASTE_VISIBLE void
g_paste_client_add_password_sync (GPasteClient *self,
                                  const gchar  *name,
                                  const gchar  *password,
                                  GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_add_password_sync (G_PASTE_DAEMON2 (self), name, password, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
}

/**
 * g_paste_client_backup_history_sync:
 * @self: a #GPasteClient instance
 * @history: the name of the history
 * @backup: the name of the backup
 * @error: return location for a #GError, or %NULL
 *
 * Backup the current history
 */
G_PASTE_VISIBLE void
g_paste_client_backup_history_sync (GPasteClient *self,
                                    const gchar  *history,
                                    const gchar  *backup,
                                    GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_backup_history_sync (G_PASTE_DAEMON2 (self), history, backup, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
}

/**
 * g_paste_client_change_passphrase_sync:
 * @self: a #GPasteClient instance
 * @error: return location for a #GError, or %NULL
 *
 * Change the passphrase of the encrypted history, re-encrypting it with the new
 * one. The daemon prompts for the passphrases itself: they never travel over the
 * bus.
 */
G_PASTE_VISIBLE void
g_paste_client_change_passphrase_sync (GPasteClient *self,
                                       GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_change_passphrase_sync (G_PASTE_DAEMON2 (self), G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
}

/**
 * g_paste_client_delete_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to delete
 * @error: return location for a #GError, or %NULL
 *
 * Delete an item from the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_delete_sync (GPasteClient *self,
                            const gchar  *uuid,
                            GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_delete_sync (G_PASTE_DAEMON2 (self), uuid, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
}

/**
 * g_paste_client_delete_history_sync:
 * @self: a #GPasteClient instance
 * @name: the name of the history to delete
 * @error: return location for a #GError, or %NULL
 *
 * Delete a history
 */
G_PASTE_VISIBLE void
g_paste_client_delete_history_sync (GPasteClient *self,
                                    const gchar  *name,
                                    GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_delete_history_sync (G_PASTE_DAEMON2 (self), name, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
}

/**
 * g_paste_client_delete_password_sync:
 * @self: a #GPasteClient instance
 * @name: the name of the password to delete
 * @error: return location for a #GError, or %NULL
 *
 * Delete the password from the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_delete_password_sync (GPasteClient *self,
                                     const gchar  *name,
                                     GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_delete_password_sync (G_PASTE_DAEMON2 (self), name, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
}

/**
 * g_paste_client_empty_history_sync:
 * @self: a #GPasteClient instance
 * @name: the name of the history to empty
 * @error: return location for a #GError, or %NULL
 *
 * Empty the history from the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_empty_history_sync (GPasteClient *self,
                                   const gchar  *name,
                                   GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_empty_history_sync (G_PASTE_DAEMON2 (self), name, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
}

/**
 * g_paste_client_get_element_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to get
 * @error: return location for a #GError, or %NULL
 *
 * Get an item from the #GPasteDaemon
 *
 * Returns: (transfer full): a newly allocated string
 */
G_PASTE_VISIBLE gchar *
g_paste_client_get_element_sync (GPasteClient *self,
                                 const gchar  *uuid,
                                 GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autofree gchar *value = NULL;
    if (!g_paste_daemon2_call_get_element_sync (G_PASTE_DAEMON2 (self), uuid, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, &value, NULL /* cancellable */, error))
        return NULL;

    return g_steal_pointer (&value);
}

/**
 * g_paste_client_get_element_at_index_sync:
 * @self: a #GPasteClient instance
 * @index: the index of the element we want to get
 * @error: return location for a #GError, or %NULL
 *
 * Get an item from the #GPasteDaemon
 *
 * Returns: (transfer full): a new #GPasteClientItem
 */
G_PASTE_VISIBLE GPasteClientItem *
g_paste_client_get_element_at_index_sync (GPasteClient *self,
                                          guint64       index,
                                          GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autofree gchar *uuid = NULL;
        g_autofree gchar *value = NULL;
    if (!g_paste_daemon2_call_get_element_at_index_sync (G_PASTE_DAEMON2 (self), index, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, &uuid, &value, NULL /* cancellable */, error))
        return NULL;

    return g_paste_client_item_new (uuid, value);
}

/**
 * g_paste_client_get_element_kind_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to get
 * @error: return location for a #GError, or %NULL
 *
 * Get the kind of an item from the #GPasteDaemon
 *
 * Returns: The #GPasteItemKind
 */
G_PASTE_VISIBLE GPasteItemKind
g_paste_client_get_element_kind_sync (GPasteClient *self,
                                      const gchar  *uuid,
                                      GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), G_PASTE_ITEM_KIND_INVALID);
    g_return_val_if_fail (!error || !(*error), G_PASTE_ITEM_KIND_INVALID);

    g_autofree gchar *kind = NULL;
    if (!g_paste_daemon2_call_get_element_kind_sync (G_PASTE_DAEMON2 (self), uuid, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, &kind, NULL /* cancellable */, error))
        return G_PASTE_ITEM_KIND_INVALID;

    const GEnumValue *k = g_enum_get_value_by_nick (g_type_class_peek (G_PASTE_TYPE_ITEM_KIND), kind);

    return (k) ? k->value : G_PASTE_ITEM_KIND_INVALID;
}

/**
 * g_paste_client_get_elements_sync:
 * @self: a #GPasteClient instance
 * @uuids: (array length=n_uuids): the uuids of the elements we want to get
 * @n_uuids: the number of uuids
 * @error: return location for a #GError, or %NULL
 *
 * Get some items from the #GPasteDaemon
 *
 * Returns: (element-type GPasteClientItem) (transfer full): a newly allocated array of string
 */
G_PASTE_VISIBLE GList *
g_paste_client_get_elements_sync (GPasteClient  *self,
                                  const gchar  **uuids,
                                  guint64        n_uuids,
                                  GError       **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autofree const gchar **terminated = g_paste_client_terminate_strv (uuids, n_uuids);
    g_autoptr (GVariant) elements = NULL;

    if (!g_paste_daemon2_call_get_elements_sync (G_PASTE_DAEMON2 (self), (const gchar * const *) terminated, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, &elements, NULL /* cancellable */, error))
        return NULL;

    return g_paste_util_get_dbus_items_result (elements);
}

/**
 * g_paste_client_get_history_sync:
 * @self: a #GPasteClient instance
 * @error: return location for a #GError, or %NULL
 *
 * Get the history from the #GPasteDaemon
 *
 * Returns: (element-type GPasteClientItem) (transfer full): a newly allocated array of string
 */
G_PASTE_VISIBLE GList *
g_paste_client_get_history_sync (GPasteClient *self,
                                 GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autoptr (GVariant) history = NULL;
    if (!g_paste_daemon2_call_get_history_sync (G_PASTE_DAEMON2 (self), G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, &history, NULL /* cancellable */, error))
        return NULL;

    return g_paste_util_get_dbus_items_result (history);
}

/**
 * g_paste_client_get_history_name_sync:
 * @self: a #GPasteClient instance
 * @error: return location for a #GError, or %NULL
 *
 * Get the name of the history from the #GPasteDaemon
 *
 * Returns: (transfer full): a newly allocated string
 */
G_PASTE_VISIBLE gchar *
g_paste_client_get_history_name_sync (GPasteClient *self,
                                      GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autofree gchar *name = NULL;
    if (!g_paste_daemon2_call_get_history_name_sync (G_PASTE_DAEMON2 (self), G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, &name, NULL /* cancellable */, error))
        return NULL;

    return g_steal_pointer (&name);
}

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
G_PASTE_VISIBLE guint64
g_paste_client_get_history_size_sync (GPasteClient *self,
                                      const gchar  *name,
                                      GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), 0);
    g_return_val_if_fail (!error || !(*error), 0);

    guint64 size = 0;
    if (!g_paste_daemon2_call_get_history_size_sync (G_PASTE_DAEMON2 (self), name, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, &size, NULL /* cancellable */, error))
        return 0;

    return size;
}

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
G_PASTE_VISIBLE GBytes *
g_paste_client_get_image_sync (GPasteClient *self,
                               const gchar  *uuid,
                               GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autoptr (GVariant) image = NULL;
    if (!g_paste_daemon2_call_get_image_sync (G_PASTE_DAEMON2 (self), uuid, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, &image, NULL /* cancellable */, error))
        return NULL;

    return g_variant_get_data_as_bytes (image);
}

/**
 * g_paste_client_get_raw_element_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to get
 * @error: return location for a #GError, or %NULL
 *
 * Get an item from the #GPasteDaemon
 *
 * Returns: (transfer full): a newly allocated string
 */
G_PASTE_VISIBLE gchar *
g_paste_client_get_raw_element_sync (GPasteClient *self,
                                     const gchar  *uuid,
                                     GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autofree gchar *value = NULL;
    if (!g_paste_daemon2_call_get_raw_element_sync (G_PASTE_DAEMON2 (self), uuid, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, &value, NULL /* cancellable */, error))
        return NULL;

    return g_steal_pointer (&value);
}

/**
 * g_paste_client_get_raw_history_sync:
 * @self: a #GPasteClient instance
 * @error: return location for a #GError, or %NULL
 *
 * Get the history from the #GPasteDaemon
 *
 * Returns: (element-type GPasteClientItem) (transfer full): a newly allocated array of string
 */
G_PASTE_VISIBLE GList *
g_paste_client_get_raw_history_sync (GPasteClient *self,
                                     GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autoptr (GVariant) history = NULL;
    if (!g_paste_daemon2_call_get_raw_history_sync (G_PASTE_DAEMON2 (self), G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, &history, NULL /* cancellable */, error))
        return NULL;

    return g_paste_util_get_dbus_items_result (history);
}

/**
 * g_paste_client_list_histories_sync:
 * @self: a #GPasteClient instance
 * @error: return location for a #GError, or %NULL
 *
 * List all available hisotries
 *
 * Returns: (transfer full): a newly allocated array of string
 */
G_PASTE_VISIBLE GStrv
g_paste_client_list_histories_sync (GPasteClient *self,
                                    GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_auto (GStrv) histories = NULL;
    if (!g_paste_daemon2_call_list_histories_sync (G_PASTE_DAEMON2 (self), G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, &histories, NULL /* cancellable */, error))
        return NULL;

    return g_steal_pointer (&histories);
}

/**
 * g_paste_client_merge_sync:
 * @self: a #GPasteClient instance
 * @decoration: (nullable): the decoration to apply to each entry
 * @separator: (nullable): the separator to add between each entry
 * @uuids: (array length=n_uuids): the uuids of the elements we want to get
 * @n_uuids: the number of uuids
 * @error: return location for a #GError, or %NULL
 *
 * Merge some history entries
 *
 * If decoration is " and separator is , and entries are foo bar baz
 * result will be "foo","bar","baz"
 */
G_PASTE_VISIBLE void
g_paste_client_merge_sync (GPasteClient  *self,
                           const gchar   *decoration,
                           const gchar   *separator,
                           const gchar  **uuids,
                           guint64        n_uuids,
                           GError       **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_autofree const gchar **terminated = g_paste_client_terminate_strv (uuids, n_uuids);

    g_paste_daemon2_call_merge_sync (G_PASTE_DAEMON2 (self),
                                     (decoration) ? decoration : "",
                                     (separator) ? separator : "",
                                     (const gchar * const *) terminated,
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1, /* timeout */
                                     NULL, /* cancellable */
                                     error);
}

/**
 * g_paste_client_on_extension_state_changed_sync:
 * @self: a #GPasteClient instance
 * @state: the new state of the extension
 * @error: return location for a #GError, or %NULL
 *
 * Call this when the extension changes its state
 */
G_PASTE_VISIBLE void
g_paste_client_on_extension_state_changed_sync (GPasteClient *self,
                                                gboolean      state,
                                                GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_on_extension_state_changed_sync (G_PASTE_DAEMON2 (self), state, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
}

/**
 * g_paste_client_reexecute_sync:
 * @self: a #GPasteClient instance
 * @error: return location for a #GError, or %NULL
 *
 * Reexecute the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_reexecute_sync (GPasteClient *self,
                               GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_reexecute_sync (G_PASTE_DAEMON2 (self), G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
}

/**
 * g_paste_client_rename_password_sync:
 * @self: a #GPasteClient instance
 * @old_name: the name of the password to rename
 * @new_name: the new name to give it
 * @error: return location for a #GError, or %NULL
 *
 * Rename the password in the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_rename_password_sync (GPasteClient *self,
                                     const gchar  *old_name,
                                     const gchar  *new_name,
                                     GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_rename_password_sync (G_PASTE_DAEMON2 (self), old_name, new_name, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
}

/**
 * g_paste_client_replace_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to replace
 * @contents: the replacement contents
 * @error: return location for a #GError, or %NULL
 *
 * Replace the contents of an item
 */
G_PASTE_VISIBLE void
g_paste_client_replace_sync (GPasteClient *self,
                             const gchar  *uuid,
                             const gchar  *contents,
                             GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_replace_sync (G_PASTE_DAEMON2 (self), uuid, contents, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
}

/**
 * g_paste_client_search_sync:
 * @self: a #GPasteClient instance
 * @pattern: the pattern to look for in history
 * @error: return location for a #GError, or %NULL
 *
 * Search for items matching @pattern in history
 *
 * Returns: (transfer full): The uuids of the matching items
 */
G_PASTE_VISIBLE GStrv
g_paste_client_search_sync (GPasteClient *self,
                            const gchar  *pattern,
                            GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_auto (GStrv) results = NULL;
    if (!g_paste_daemon2_call_search_sync (G_PASTE_DAEMON2 (self), pattern, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, &results, NULL /* cancellable */, error))
        return NULL;

    return g_steal_pointer (&results);
}

/**
 * g_paste_client_select_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to select
 * @error: return location for a #GError, or %NULL
 *
 * Select an item from the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_select_sync (GPasteClient *self,
                            const gchar  *uuid,
                            GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_select_sync (G_PASTE_DAEMON2 (self), uuid, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
}

/**
 * g_paste_client_set_password_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to set as password
 * @name: the name to identify the password
 * @error: return location for a #GError, or %NULL
 *
 * Set the item as password
 */
G_PASTE_VISIBLE void
g_paste_client_set_password_sync (GPasteClient *self,
                                  const gchar  *uuid,
                                  const gchar  *name,
                                  GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_set_password_sync (G_PASTE_DAEMON2 (self), uuid, name, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
}

/**
 * g_paste_client_show_history_sync:
 * @self: a #GPasteClient instance
 * @error: return location for a #GError, or %NULL
 *
 * Emit the ShowHistory signal
 */
G_PASTE_VISIBLE void
g_paste_client_show_history_sync (GPasteClient *self,
                                  GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_show_history_sync (G_PASTE_DAEMON2 (self), G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
}
/**
 * g_paste_client_switch_history_sync:
 * @self: a #GPasteClient instance
 * @name: the name of the history to switch to
 * @error: return location for a #GError, or %NULL
 *
 * Switch to another history
 */
G_PASTE_VISIBLE void
g_paste_client_switch_history_sync (GPasteClient *self,
                                    const gchar  *name,
                                    GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_switch_history_sync (G_PASTE_DAEMON2 (self), name, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
}

/**
 * g_paste_client_track_sync:
 * @self: a #GPasteClient instance
 * @state: the new tracking state of the #GPasteDaemon
 * @error: return location for a #GError, or %NULL
 *
 * Change the tracking state of the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_track_sync (GPasteClient *self,
                           gboolean      state,
                           GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_track_sync (G_PASTE_DAEMON2 (self), state, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
}

/**
 * g_paste_client_upload_sync:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to upload
 * @error: return location for a #GError, or %NULL
 *
 * Upload an item to a pastebin service
 */
G_PASTE_VISIBLE void
g_paste_client_upload_sync (GPasteClient *self,
                            const gchar  *uuid,
                            GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_upload_sync (G_PASTE_DAEMON2 (self), uuid, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, error);
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
 */
G_PASTE_VISIBLE void
g_paste_client_about (GPasteClient       *self,
                      GAsyncReadyCallback callback,
                      gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_about (G_PASTE_DAEMON2 (self), G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
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
 */
G_PASTE_VISIBLE void
g_paste_client_add (GPasteClient       *self,
                    const gchar        *text,
                    GAsyncReadyCallback callback,
                    gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_add (G_PASTE_DAEMON2 (self), text, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
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
g_paste_client_add_file (GPasteClient       *self,
                         const gchar        *file,
                         GAsyncReadyCallback callback,
                         gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_autofree gchar *absolute_path = NULL;

    if (!g_path_is_absolute (file))
    {
        g_autofree gchar *current_dir = g_get_current_dir ();
        absolute_path = g_build_filename (current_dir, file, NULL);
    }

    g_paste_daemon2_call_add_file (G_PASTE_DAEMON2 (self), (absolute_path) ? absolute_path : file, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
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
 */
G_PASTE_VISIBLE void
g_paste_client_add_password (GPasteClient       *self,
                             const gchar        *name,
                             const gchar        *password,
                             GAsyncReadyCallback callback,
                             gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_add_password (G_PASTE_DAEMON2 (self), name, password, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

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
G_PASTE_VISIBLE void
g_paste_client_backup_history (GPasteClient       *self,
                               const gchar        *history,
                               const gchar        *backup,
                               GAsyncReadyCallback callback,
                               gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_backup_history (G_PASTE_DAEMON2 (self), history, backup, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

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
G_PASTE_VISIBLE void
g_paste_client_change_passphrase (GPasteClient       *self,
                                  GAsyncReadyCallback callback,
                                  gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_change_passphrase (G_PASTE_DAEMON2 (self), G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

/**
 * g_paste_client_delete:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to delete
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Delete an item from the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_delete (GPasteClient       *self,
                       const gchar        *uuid,
                       GAsyncReadyCallback callback,
                       gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_delete (G_PASTE_DAEMON2 (self), uuid, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

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
G_PASTE_VISIBLE void
g_paste_client_delete_history (GPasteClient       *self,
                               const gchar        *name,
                               GAsyncReadyCallback callback,
                               gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_delete_history (G_PASTE_DAEMON2 (self), name, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
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
 */
G_PASTE_VISIBLE void
g_paste_client_delete_password (GPasteClient       *self,
                                const gchar        *name,
                                GAsyncReadyCallback callback,
                                gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_delete_password (G_PASTE_DAEMON2 (self), name, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

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
G_PASTE_VISIBLE void
g_paste_client_empty_history (GPasteClient       *self,
                              const gchar        *name,
                              GAsyncReadyCallback callback,
                              gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_empty_history (G_PASTE_DAEMON2 (self), name, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

/**
 * g_paste_client_get_element:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to get
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get an item from the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_get_element (GPasteClient       *self,
                            const gchar        *uuid,
                            GAsyncReadyCallback callback,
                            gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_get_element (G_PASTE_DAEMON2 (self), uuid, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

/**
 * g_paste_client_get_element_at_index:
 * @self: a #GPasteClient instance
 * @index: the index of the element we want to get
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get an item from the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_get_element_at_index (GPasteClient       *self,
                                     guint64             index,
                                     GAsyncReadyCallback callback,
                                     gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_get_element_at_index (G_PASTE_DAEMON2 (self), index, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

/**
 * g_paste_client_get_element_kind:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to get
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get the kind of an item from the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_get_element_kind (GPasteClient       *self,
                                 const gchar        *uuid,
                                 GAsyncReadyCallback callback,
                                 gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_get_element_kind (G_PASTE_DAEMON2 (self), uuid, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

/**
 * g_paste_client_get_elements:
 * @self: a #GPasteClient instance
 * @uuids: (array length=n_uuids): the uuids of the elements we want to get
 * @n_uuids: the number of uuids
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get some items from the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_get_elements (GPasteClient       *self,
                             const gchar       **uuids,
                             guint64             n_uuids,
                             GAsyncReadyCallback callback,
                             gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_autofree const gchar **terminated = g_paste_client_terminate_strv (uuids, n_uuids);

    g_paste_daemon2_call_get_elements (G_PASTE_DAEMON2 (self), (const gchar * const *) terminated, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

/**
 * g_paste_client_get_history:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get the history from the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_get_history (GPasteClient       *self,
                            GAsyncReadyCallback callback,
                            gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_get_history (G_PASTE_DAEMON2 (self), G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

/**
 * g_paste_client_get_history_name:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get the name of the history from the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_get_history_name (GPasteClient       *self,
                                 GAsyncReadyCallback callback,
                                 gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_get_history_name (G_PASTE_DAEMON2 (self), G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

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
G_PASTE_VISIBLE void
g_paste_client_get_history_size (GPasteClient       *self,
                                 const gchar        *name,
                                 GAsyncReadyCallback callback,
                                 gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_get_history_size (G_PASTE_DAEMON2 (self), name, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

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
G_PASTE_VISIBLE void
g_paste_client_get_image (GPasteClient       *self,
                          const gchar        *uuid,
                          GAsyncReadyCallback callback,
                          gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_get_image (G_PASTE_DAEMON2 (self), uuid, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

/**
 * g_paste_client_get_raw_element:
 * @self: a #GPasteClient instance
 * @uuid: the uuid of the element we want to get
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get an item from the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_get_raw_element (GPasteClient       *self,
                                const gchar        *uuid,
                                GAsyncReadyCallback callback,
                                gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_get_raw_element (G_PASTE_DAEMON2 (self), uuid, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

/**
 * g_paste_client_get_raw_history:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Get the history from the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_get_raw_history (GPasteClient       *self,
                                GAsyncReadyCallback callback,
                                gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_get_raw_history (G_PASTE_DAEMON2 (self), G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

/**
 * g_paste_client_list_histories:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * List all available hisotries
 */
G_PASTE_VISIBLE void
g_paste_client_list_histories (GPasteClient       *self,
                               GAsyncReadyCallback callback,
                               gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_list_histories (G_PASTE_DAEMON2 (self), G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

/**
 * g_paste_client_merge:
 * @self: a #GPasteClient instance
 * @decoration: (nullable): the decoration to apply to each entry
 * @separator: (nullable): the separator to add between each entry
 * @uuids: (array length=n_uuids): the uuids of the elements we want to get
 * @n_uuids: the number of uuids
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
g_paste_client_merge (GPasteClient       *self,
                      const gchar        *decoration,
                      const gchar        *separator,
                      const gchar       **uuids,
                      guint64             n_uuids,
                      GAsyncReadyCallback callback,
                      gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_autofree const gchar **terminated = g_paste_client_terminate_strv (uuids, n_uuids);

    g_paste_daemon2_call_merge (G_PASTE_DAEMON2 (self),
                                (decoration) ? decoration : "",
                                (separator) ? separator : "",
                                (const gchar * const *) terminated,
                                G_DBUS_CALL_FLAGS_NONE,
                                -1, /* timeout */
                                NULL, /* cancellable */
                                callback,
                                user_data);
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
 */
G_PASTE_VISIBLE void
g_paste_client_on_extension_state_changed (GPasteClient       *self,
                                           gboolean            state,
                                           GAsyncReadyCallback callback,
                                           gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_on_extension_state_changed (G_PASTE_DAEMON2 (self), state, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

/**
 * g_paste_client_reexecute:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Reexecute the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_reexecute (GPasteClient       *self,
                          GAsyncReadyCallback callback,
                          gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_reexecute (G_PASTE_DAEMON2 (self), G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
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
 */
G_PASTE_VISIBLE void
g_paste_client_rename_password (GPasteClient       *self,
                                const gchar        *old_name,
                                const gchar        *new_name,
                                GAsyncReadyCallback callback,
                                gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_rename_password (G_PASTE_DAEMON2 (self), old_name, new_name, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

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
G_PASTE_VISIBLE void
g_paste_client_replace (GPasteClient       *self,
                        const gchar        *uuid,
                        const gchar        *contents,
                        GAsyncReadyCallback callback,
                        gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_replace (G_PASTE_DAEMON2 (self), uuid, contents, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
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
 */
G_PASTE_VISIBLE void
g_paste_client_search (GPasteClient       *self,
                       const gchar        *pattern,
                       GAsyncReadyCallback callback,
                       gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_search (G_PASTE_DAEMON2 (self), pattern, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

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
G_PASTE_VISIBLE void
g_paste_client_select (GPasteClient       *self,
                       const gchar        *uuid,
                       GAsyncReadyCallback callback,
                       gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_select (G_PASTE_DAEMON2 (self), uuid, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

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
G_PASTE_VISIBLE void
g_paste_client_set_password (GPasteClient       *self,
                             const gchar        *uuid,
                             const gchar        *name,
                             GAsyncReadyCallback callback,
                             gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_set_password (G_PASTE_DAEMON2 (self), uuid, name, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

/**
 * g_paste_client_show_history:
 * @self: a #GPasteClient instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Emit the ShowHistory signal
 */
G_PASTE_VISIBLE void
g_paste_client_show_history (GPasteClient       *self,
                             GAsyncReadyCallback callback,
                             gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_show_history (G_PASTE_DAEMON2 (self), G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
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
 */
G_PASTE_VISIBLE void
g_paste_client_switch_history (GPasteClient       *self,
                               const gchar        *name,
                               GAsyncReadyCallback callback,
                               gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_switch_history (G_PASTE_DAEMON2 (self), name, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
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
 */
G_PASTE_VISIBLE void
g_paste_client_track (GPasteClient *self,
                      gboolean      state,
                      GAsyncReadyCallback callback,
                      gpointer             user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_track (G_PASTE_DAEMON2 (self), state, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

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
G_PASTE_VISIBLE void
g_paste_client_upload (GPasteClient       *self,
                       const gchar        *uuid,
                       GAsyncReadyCallback callback,
                       gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));

    g_paste_daemon2_call_upload (G_PASTE_DAEMON2 (self), uuid, G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */, NULL /* cancellable */, callback, user_data);
}

/****************************/
/* Methods / Async - Finish */
/****************************/

/**
 * g_paste_client_about_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Display the about dialog
 */
G_PASTE_VISIBLE void
g_paste_client_about_finish (GPasteClient *self,
                             GAsyncResult *result,
                             GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_about_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_add_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Add an item to the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_add_finish (GPasteClient *self,
                           GAsyncResult *result,
                           GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_add_finish (G_PASTE_DAEMON2 (self), result, error);
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

    g_paste_daemon2_call_add_file_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_add_password_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Add the password to the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_add_password_finish (GPasteClient *self,
                                    GAsyncResult *result,
                                    GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_add_password_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_backup_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Backup the current history
 */
G_PASTE_VISIBLE void
g_paste_client_backup_history_finish (GPasteClient *self,
                                      GAsyncResult *result,
                                      GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_backup_history_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_change_passphrase_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Change the passphrase of the encrypted history
 */
G_PASTE_VISIBLE void
g_paste_client_change_passphrase_finish (GPasteClient *self,
                                         GAsyncResult *result,
                                         GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_change_passphrase_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_delete_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Delete an item from the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_delete_finish (GPasteClient *self,
                              GAsyncResult *result,
                              GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_delete_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_delete_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Delete a history
 */
G_PASTE_VISIBLE void
g_paste_client_delete_history_finish (GPasteClient *self,
                                      GAsyncResult *result,
                                      GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_delete_history_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_delete_password_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Delete the password from the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_delete_password_finish (GPasteClient *self,
                                       GAsyncResult *result,
                                       GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_delete_password_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_empty_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Empty the history from the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_empty_history_finish (GPasteClient *self,
                                     GAsyncResult *result,
                                     GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_empty_history_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_get_element_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Get an item from the #GPasteDaemon
 *
 * Returns: (transfer full): a newly allocated string
 */
G_PASTE_VISIBLE gchar *
g_paste_client_get_element_finish (GPasteClient *self,
                                   GAsyncResult *result,
                                   GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autofree gchar *value = NULL;
    if (!g_paste_daemon2_call_get_element_finish (G_PASTE_DAEMON2 (self), &value, result, error))
        return NULL;

    return g_steal_pointer (&value);
}

/**
 * g_paste_client_get_element_at_index_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Get an item from the #GPasteDaemon
 *
 * Returns: (transfer full): a new #GPasteClientItem
 */
G_PASTE_VISIBLE GPasteClientItem *
g_paste_client_get_element_at_index_finish (GPasteClient *self,
                                            GAsyncResult *result,
                                            GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autofree gchar *uuid = NULL;
        g_autofree gchar *value = NULL;
    if (!g_paste_daemon2_call_get_element_at_index_finish (G_PASTE_DAEMON2 (self), &uuid, &value, result, error))
        return NULL;

    return g_paste_client_item_new (uuid, value);
}

/**
 * g_paste_client_get_element_kind_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Get this kind of an item from the #GPasteDaemon
 *
 * Returns: The #GPasteItemKind
 */
G_PASTE_VISIBLE GPasteItemKind
g_paste_client_get_element_kind_finish (GPasteClient *self,
                                        GAsyncResult *result,
                                        GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), G_PASTE_ITEM_KIND_INVALID);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), G_PASTE_ITEM_KIND_INVALID);
    g_return_val_if_fail (!error || !(*error), G_PASTE_ITEM_KIND_INVALID);

    g_autofree gchar *kind = NULL;
    if (!g_paste_daemon2_call_get_element_kind_finish (G_PASTE_DAEMON2 (self), &kind, result, error))
        return G_PASTE_ITEM_KIND_INVALID;

    const GEnumValue *k = g_enum_get_value_by_nick (g_type_class_peek (G_PASTE_TYPE_ITEM_KIND), kind);

    return (k) ? k->value : G_PASTE_ITEM_KIND_INVALID;
}

/**
 * g_paste_client_get_elements_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Get some items from the #GPasteDaemon
 *
 * Returns: (element-type GPasteClientItem) (transfer full): a newly allocated array of string
 */
G_PASTE_VISIBLE GList *
g_paste_client_get_elements_finish (GPasteClient *self,
                                    GAsyncResult *result,
                                    GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autoptr (GVariant) elements = NULL;

    if (!g_paste_daemon2_call_get_elements_finish (G_PASTE_DAEMON2 (self), &elements, result, error))
        return NULL;

    return g_paste_util_get_dbus_items_result (elements);
}

/**
 * g_paste_client_get_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Get the history from the #GPasteDaemon
 *
 * Returns: (element-type GPasteClientItem) (transfer full): a newly allocated array of string
 */
G_PASTE_VISIBLE GList *
g_paste_client_get_history_finish (GPasteClient *self,
                                   GAsyncResult *result,
                                   GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autoptr (GVariant) history = NULL;
    if (!g_paste_daemon2_call_get_history_finish (G_PASTE_DAEMON2 (self), &history, result, error))
        return NULL;

    return g_paste_util_get_dbus_items_result (history);
}

/**
 * g_paste_client_get_history_name_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Get the name of the history from the #GPasteDaemon
 *
 * Returns: (transfer full): a newly allocated string
 */
G_PASTE_VISIBLE gchar *
g_paste_client_get_history_name_finish (GPasteClient *self,
                                        GAsyncResult *result,
                                        GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autofree gchar *name = NULL;
    if (!g_paste_daemon2_call_get_history_name_finish (G_PASTE_DAEMON2 (self), &name, result, error))
        return NULL;

    return g_steal_pointer (&name);
}

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
G_PASTE_VISIBLE guint64
g_paste_client_get_history_size_finish (GPasteClient *self,
                                        GAsyncResult *result,
                                        GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), 0);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), 0);
    g_return_val_if_fail (!error || !(*error), 0);

    guint64 size = 0;
    if (!g_paste_daemon2_call_get_history_size_finish (G_PASTE_DAEMON2 (self), &size, result, error))
        return 0;

    return size;
}

/**
 * g_paste_client_get_raw_element_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Get an item from the #GPasteDaemon
 *
 * Returns: (transfer full): a newly allocated string
 */
G_PASTE_VISIBLE gchar *
g_paste_client_get_raw_element_finish (GPasteClient *self,
                                       GAsyncResult *result,
                                       GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autofree gchar *value = NULL;
    if (!g_paste_daemon2_call_get_raw_element_finish (G_PASTE_DAEMON2 (self), &value, result, error))
        return NULL;

    return g_steal_pointer (&value);
}

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
G_PASTE_VISIBLE GBytes *
g_paste_client_get_image_finish (GPasteClient *self,
                                 GAsyncResult *result,
                                 GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autoptr (GVariant) image = NULL;
    if (!g_paste_daemon2_call_get_image_finish (G_PASTE_DAEMON2 (self), &image, result, error))
        return NULL;

    return g_variant_get_data_as_bytes (image);
}

/**
 * g_paste_client_get_raw_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Get the history from the #GPasteDaemon
 *
 * Returns: (element-type GPasteClientItem) (transfer full): a newly allocated array of string
 */
G_PASTE_VISIBLE GList *
g_paste_client_get_raw_history_finish (GPasteClient *self,
                                       GAsyncResult *result,
                                       GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autoptr (GVariant) history = NULL;
    if (!g_paste_daemon2_call_get_raw_history_finish (G_PASTE_DAEMON2 (self), &history, result, error))
        return NULL;

    return g_paste_util_get_dbus_items_result (history);
}

/**
 * g_paste_client_list_histories_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
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
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_auto (GStrv) histories = NULL;
    if (!g_paste_daemon2_call_list_histories_finish (G_PASTE_DAEMON2 (self), &histories, result, error))
        return NULL;

    return g_steal_pointer (&histories);
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

    g_paste_daemon2_call_merge_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_on_extension_state_changed_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Call this when the extension changes its state
 */
G_PASTE_VISIBLE void
g_paste_client_on_extension_state_changed_finish (GPasteClient *self,
                                                  GAsyncResult *result,
                                                  GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_on_extension_state_changed_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_reexecute_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Reexecute the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_reexecute_finish (GPasteClient *self,
                                 GAsyncResult *result,
                                 GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_reexecute_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_rename_password_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Rename the password in the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_rename_password_finish (GPasteClient *self,
                                       GAsyncResult *result,
                                       GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_rename_password_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_replace_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Replace the contents of an item
 */
G_PASTE_VISIBLE void
g_paste_client_replace_finish (GPasteClient *self,
                               GAsyncResult *result,
                               GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_replace_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_search_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Search for items matching @pattern in history
 *
 * Returns: (transfer full): The indexes of the matching items
 */
G_PASTE_VISIBLE GStrv
g_paste_client_search_finish (GPasteClient *self,
                              GAsyncResult *result,
                              GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), NULL);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_auto (GStrv) results = NULL;
    if (!g_paste_daemon2_call_search_finish (G_PASTE_DAEMON2 (self), &results, result, error))
        return NULL;

    return g_steal_pointer (&results);
}

/**
 * g_paste_client_select_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Select an item from the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_select_finish (GPasteClient *self,
                              GAsyncResult *result,
                              GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_select_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_set_password_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Set the item as password
 */
G_PASTE_VISIBLE void
g_paste_client_set_password_finish (GPasteClient *self,
                                    GAsyncResult *result,
                                    GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_set_password_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_show_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Emit the ShowHistory signal
 */
G_PASTE_VISIBLE void
g_paste_client_show_history_finish (GPasteClient *self,
                                    GAsyncResult *result,
                                    GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_show_history_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_switch_history_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Switch to another history
 */
G_PASTE_VISIBLE void
g_paste_client_switch_history_finish (GPasteClient *self,
                                      GAsyncResult *result,
                                      GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_switch_history_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_track_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Change the tracking state of the #GPasteDaemon
 */
G_PASTE_VISIBLE void
g_paste_client_track_finish (GPasteClient *self,
                             GAsyncResult *result,
                             GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_track_finish (G_PASTE_DAEMON2 (self), result, error);
}

/**
 * g_paste_client_upload_finish:
 * @self: a #GPasteClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: return location for a #GError, or %NULL
 *
 * Upload an item to a pastebin service
 */
G_PASTE_VISIBLE void
g_paste_client_upload_finish (GPasteClient *self,
                              GAsyncResult *result,
                              GError      **error)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (self));
    g_return_if_fail (G_IS_ASYNC_RESULT (result));
    g_return_if_fail (!error || !(*error));

    g_paste_daemon2_call_upload_finish (G_PASTE_DAEMON2 (self), result, error);
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
    g_return_val_if_fail (G_PASTE_IS_CLIENT (self), FALSE);

    g_autoptr (GVariant) active = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), G_PASTE_DAEMON_PROP_ACTIVE);

    return (active) ? g_variant_get_boolean (active) : FALSE;
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

    g_autoptr (GVariant) version = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), G_PASTE_DAEMON_PROP_VERSION);

    return (version) ? g_variant_dup_string (version, NULL) : NULL;
}

static void
g_paste_client_daemon2_iface_init (GPasteDaemon2Iface *iface G_GNUC_UNUSED)
{
    /* Nothing to fill in: the interface is implemented for its client half, and
     * the generated g_paste_daemon2_call_*() go straight through GDBusProxy.
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
    case PROP_VERSION:
        g_value_take_string (value, g_paste_client_get_version (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

/* Both properties are read-only on the wire. The interface declares them
 * writable all the same, so overriding them requires a setter to exist, but
 * there is nothing a client could set: say so rather than pretend. */
static void
g_paste_client_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value G_GNUC_UNUSED,
                             GParamSpec   *pspec)
{
    switch (prop_id)
    {
    case PROP_ACTIVE:
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
    else if (g_paste_str_equal (signal_name, G_PASTE_DAEMON_SIG_SWITCH_HISTORY))
    {
        g_variant_get (parameters, "(&s)", &history);
        g_signal_emit (self, signals[SWITCH_HISTORY], 0 /* detail */, history);
    }
    else if (g_paste_str_equal (signal_name, G_PASTE_DAEMON_SIG_UPDATE))
    {
        const gchar *action_nick, *target_nick;
        guint64 index;

        g_variant_get (parameters, "(&s&st)", &action_nick, &target_nick, &index);

        const GEnumValue *action = g_enum_get_value_by_nick (g_type_class_peek (G_PASTE_TYPE_UPDATE_ACTION), action_nick);
        const GEnumValue *target = g_enum_get_value_by_nick (g_type_class_peek (G_PASTE_TYPE_UPDATE_TARGET), target_nick);

        /* A daemon newer than us can name an action or a target we do not know —
         * which is exactly what a re-exec after an upgrade leaves us talking to,
         * with this very signal arriving in a gnome-shell that still runs the old
         * library. Skip such an update rather than dereference NULL. */
        if (!action || !target)
        {
            g_warning ("Ignoring an update from a daemon speaking of an unknown action or target");
            return;
        }

        g_signal_emit (self, signals[UPDATE], 0 /* detail */, action->value, target->value, index);
    }
}

static void
g_paste_client_g_properties_changed (GDBusProxy          *proxy,
                                     GVariant            *changed_properties,
                                     const gchar * const *invalidated_properties G_GNUC_UNUSED)
{
    GPasteClient *self = G_PASTE_CLIENT (proxy);
    GVariantDict dict;

    g_variant_dict_init (&dict, changed_properties);

    if (g_variant_dict_contains (&dict, G_PASTE_DAEMON_PROP_ACTIVE))
    {
        g_object_notify (G_OBJECT (self), "active");
        g_signal_emit (self, signals[TRACKING], 0 /* detail */, g_paste_client_is_active (self));
    }

    if (g_variant_dict_contains (&dict, G_PASTE_DAEMON_PROP_VERSION))
        g_object_notify (G_OBJECT (self), "version");

    g_variant_dict_clear (&dict);
}

static void
g_paste_client_class_init (GPasteClientClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GDBusProxyClass *proxy_class = G_DBUS_PROXY_CLASS (klass);

    object_class->get_property = g_paste_client_get_property;
    object_class->set_property = g_paste_client_set_property;

    proxy_class->g_signal = g_paste_client_g_signal;
    proxy_class->g_properties_changed = g_paste_client_g_properties_changed;

    /* Installs the interface's "Active" and "Version" on us, in the PROP_* order
     * declared above. */
    g_paste_daemon2_override_properties (object_class, PROP_ACTIVE);

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
     * GPasteClient::show-history:
     * @client: the object on which the signal was emitted
     *
     * The "show-history" signal is emitted when we switch
     * from a history to another.
     */
    signals[SHOW_HISTORY] = NEW_SIGNAL ("show-history");

    /**
     * GPasteClient::switch-history:
     * @client: the object on which the signal was emitted
     * @history: the name of the history we switch to
     *
     * The "switch-history" signal is emitted when we switch
     * from a history to another.
     */
    signals[SWITCH_HISTORY] = NEW_SIGNAL_WITH_DATA ("switch-history", STRING);

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
     * @index: the index of the item, when the target is POSITION
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
                                    3, /* number of params */
                                    G_PASTE_TYPE_UPDATE_ACTION,
                                    G_PASTE_TYPE_UPDATE_TARGET,
                                    G_TYPE_UINT64);
}

static void
g_paste_client_init (GPasteClient *self)
{
    /* Straight out of the generated binding, so the wire format this proxy
     * expects and the one the daemon serves cannot drift apart. */
    g_dbus_proxy_set_interface_info (G_DBUS_PROXY (self), g_paste_daemon2_interface_info ());
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
