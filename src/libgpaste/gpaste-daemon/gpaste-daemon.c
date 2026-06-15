// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-clipboard-gdk.h>
#include <gpaste-daemon/gpaste-clipboards-manager.h>
#include <gpaste-daemon/gpaste-daemon-methods.h>
#include <gpaste-daemon/gpaste-daemon.h>
#include <gpaste-daemon/gpaste-history.h>
#include <gpaste-daemon/gpaste-keybinder.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-global-shortcut-client.h>

#include <string.h>

struct _GPasteDaemon
{
    GPasteBusObject parent_instance;
};

typedef struct
{
    GDBusConnection         *connection;
    guint64                  id_on_bus;
    gboolean                 registered;

    GPasteHistory           *history;
    GPasteSettings          *settings;
    GPasteClipboardsManager *clipboards_manager;
    GPasteKeybinder         *keybinder;
    GPasteScreensaverClient *screensaver;

    GDBusNodeInfo           *g_paste_daemon_dbus_info;
    GDBusInterfaceVTable     g_paste_daemon_dbus_vtable;

    GSignalGroup            *history_signals;
    GSignalGroup            *settings_signals;
    GSignalGroup            *screensaver_signals;
} GPasteDaemonPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (Daemon, daemon, G_PASTE_TYPE_BUS_OBJECT)

enum
{
    REEXECUTE_SELF,

    LAST_SIGNAL
};

static guint64 signals[LAST_SIGNAL] = { 0 };

/****************/
/* DBus Signals */
/****************/

static void
g_paste_daemon_update (GPasteDaemon      *self,
                       GPasteUpdateAction action,
                       GPasteUpdateTarget target,
                       guint64            position)
{
    const GPasteDaemonPrivate *priv = _g_paste_daemon_get_instance_private (self);

    GVariant *data[] = {
        g_variant_new_string (g_enum_get_value (g_type_class_peek (G_PASTE_TYPE_UPDATE_ACTION), action)->value_nick),
        g_variant_new_string (g_enum_get_value (g_type_class_peek (G_PASTE_TYPE_UPDATE_TARGET), target)->value_nick),
        g_variant_new_uint64 (position)
    };
    G_PASTE_SEND_DBUS_SIGNAL_FULL (UPDATE, g_variant_new_tuple (data, 3), NULL);
}

/**
 * g_paste_daemon_show_history:
 * @self: (transfer none): the #GPasteDaemon
 * @error: a #GError
 *
 * Emit the signal to show history
 */
G_PASTE_VISIBLE void
g_paste_daemon_show_history (GPasteDaemon *self,
                             GError      **error)
{
    g_return_if_fail (_G_PASTE_IS_DAEMON (self));

    const GPasteDaemonPrivate *priv = _g_paste_daemon_get_instance_private (self);

    G_PASTE_SEND_DBUS_SIGNAL_WITH_ERROR (SHOW_HISTORY);
}

static void
g_paste_daemon_tracking (GPasteDaemon   *self,
                         gboolean        tracking_state,
                         GPasteSettings *settings G_GNUC_UNUSED)
{
    const GPasteDaemonPrivate *priv = _g_paste_daemon_get_instance_private (self);
    GVariant *variant = g_variant_new_boolean (tracking_state);

    G_PASTE_SEND_DBUS_PROPERTIES_CHANGED (G_PASTE_DAEMON_PROP_ACTIVE, variant);
}

/********************/
/* Daemon controls  */
/********************/

static void
g_paste_daemon_reexecute (GPasteDaemon *self)
{
    const GPasteDaemonPrivate *priv = _g_paste_daemon_get_instance_private (self);

    g_paste_clipboards_manager_store (priv->clipboards_manager);

    g_signal_emit (self,
                   signals[REEXECUTE_SELF],
                   0, /* detail */
                   NULL);
}

static void
g_paste_daemon_upload_finish (GObject      *source_object,
                              GAsyncResult *res,
                              gpointer      user_data)
{
    g_autoptr (GSubprocess) upload = G_SUBPROCESS (source_object);
    g_autofree gchar *url = NULL;
    g_autofree GPasteDBusError *err = NULL;
    GPasteDaemonPrivate *priv = user_data;
    GPasteDaemonMethods methods = {
        priv->connection,
        priv->history,
        priv->settings,
        priv->clipboards_manager
    };

    g_autoptr (GError) error = NULL;
    if (!g_subprocess_communicate_utf8_finish (upload, res, &url, NULL, &error))
        g_warning ("Upload failed: %s", error->message);

    if (url)
        g_paste_daemon_methods_do_add (&methods, url, strlen (url), &err);
}

/**
 * g_paste_daemon_upload:
 * @self: (transfer none): the #GPasteDaemon
 * @uuid: the uuid of the item to upload
 *
 * Upload an item to a pastebin service
 *
 * Returns: whether there was something to upload
 */
G_PASTE_VISIBLE gboolean
g_paste_daemon_upload (GPasteDaemon *self,
                       const gchar  *uuid)
{
    g_return_val_if_fail (_G_PASTE_IS_DAEMON (self), FALSE);

    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);
    const GPasteItem *item = (uuid) ? g_paste_history_get_by_uuid (priv->history, uuid) : g_paste_history_get (priv->history, 0);

    if (!item)
        return FALSE;

    g_autoptr (GError) error = NULL;
    GSubprocess *upload = g_subprocess_new (G_SUBPROCESS_FLAGS_STDIN_PIPE|G_SUBPROCESS_FLAGS_STDOUT_PIPE, &error, "wgetpaste", NULL);

    if (!upload)
    {
        g_warning ("Failed to spawn wgetpaste: %s", error->message);
        return FALSE;
    }

    const gchar *value = g_paste_item_get_value (item);

    g_subprocess_communicate_utf8_async (upload,
                                         value,
                                         NULL, /* cancellable */
                                         g_paste_daemon_upload_finish,
                                         priv);
    return TRUE;
}

static void
_g_paste_daemon_upload (GPasteDaemon     *self,
                        GVariant         *parameters,
                        GPasteDBusError **err)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autoptr (GVariant) variant = g_variant_iter_next_value (&parameters_iter);
    g_autofree gchar *uuid = g_variant_dup_string (variant, NULL);

    G_PASTE_DBUS_ASSERT (g_paste_daemon_upload (self, uuid), "Provided uuid doesn't match any item.");
}

/****************/
/* Keybindings  */
/****************/

static void
keybinding_make_password (GPasteKeybinding *self G_GNUC_UNUSED,
                          gpointer          data)
{
    GPasteHistory *history = data;
    const GPasteItem *first = g_paste_history_get (history, 0);

    if (!first)
        return;

    g_paste_history_set_password (history, g_paste_item_get_uuid (first), NULL);
}

static void
keybinding_pop (GPasteKeybinding *self G_GNUC_UNUSED,
                gpointer          data)
{
    g_paste_history_remove (data, 0);
}

static void
keybinding_show_history (GPasteKeybinding *self G_GNUC_UNUSED,
                         gpointer          data)
{
    g_autoptr (GError) error = NULL;

    g_paste_daemon_show_history (data, &error);
    if (error)
        g_warning ("Failed to show history: %s", error->message);
}

static void
keybinding_sync_clipboard_to_primary (GPasteKeybinding *self G_GNUC_UNUSED,
                                      gpointer          data)
{
    g_paste_clipboards_manager_sync_from_to (data, TRUE);
}

static void
keybinding_sync_primary_to_clipboard (GPasteKeybinding *self G_GNUC_UNUSED,
                                      gpointer          data)
{
    g_paste_clipboards_manager_sync_from_to (data, FALSE);
}

static void
keybinding_launch_ui (GPasteKeybinding *self G_GNUC_UNUSED,
                      gpointer          data G_GNUC_UNUSED)
{
    g_paste_util_spawn ("Ui");
}

static void
keybinding_upload (GPasteKeybinding *self G_GNUC_UNUSED,
                   gpointer          data)
{
    g_paste_daemon_upload (data, NULL);
}

static void
g_paste_daemon_activate_default_keybindings (GPasteDaemon *self)
{
    const GPasteDaemonPrivate *priv = _g_paste_daemon_get_instance_private (self);
    GPasteKeybinder *keybinder = priv->keybinder;
    GPasteHistory *history = priv->history;
    GPasteClipboardsManager *clipboards_manager = priv->clipboards_manager;
    GPasteKeybinding *keybindings[] = {
        g_paste_keybinding_new (G_PASTE_MAKE_PASSWORD_SETTING, _("Convert to Password"),
                                g_paste_settings_get_make_password, keybinding_make_password, history),
        g_paste_keybinding_new (G_PASTE_POP_SETTING, _("Pop from History"),
                                g_paste_settings_get_pop, keybinding_pop, history),
        g_paste_keybinding_new (G_PASTE_SHOW_HISTORY_SETTING, _("Show History"),
                                g_paste_settings_get_show_history, keybinding_show_history, self),
        g_paste_keybinding_new (G_PASTE_SYNC_CLIPBOARD_TO_PRIMARY_SETTING, _("Sync Clipboard to Primary"),
                                g_paste_settings_get_sync_clipboard_to_primary, keybinding_sync_clipboard_to_primary, clipboards_manager),
        g_paste_keybinding_new (G_PASTE_SYNC_PRIMARY_TO_CLIPBOARD_SETTING, _("Sync Primary to Clipboard"),
                                g_paste_settings_get_sync_primary_to_clipboard, keybinding_sync_primary_to_clipboard, clipboards_manager),
        g_paste_keybinding_new (G_PASTE_LAUNCH_UI_SETTING, _("Launch UI"),
                                g_paste_settings_get_launch_ui, keybinding_launch_ui, NULL),
        g_paste_keybinding_new (G_PASTE_UPLOAD_SETTING, _("Upload to Pastebin"),
                                g_paste_settings_get_upload, keybinding_upload, self)
    };

    for (guint64 k = 0; k < G_N_ELEMENTS (keybindings); ++k)
        g_paste_keybinder_add_keybinding (keybinder, keybindings[k]);

    g_paste_keybinder_activate_all (keybinder);
}

/****************/
/* DBus Methods */
/****************/

static void
g_paste_daemon_dbus_method_call (GDBusConnection       *connection     G_GNUC_UNUSED,
                                 const gchar           *sender         G_GNUC_UNUSED,
                                 const gchar           *object_path    G_GNUC_UNUSED,
                                 const gchar           *interface_name G_GNUC_UNUSED,
                                 const gchar           *method_name,
                                 GVariant              *parameters,
                                 GDBusMethodInvocation *invocation,
                                 gpointer               user_data)
{
    GPasteDaemon *self = user_data;
    const GPasteDaemonPrivate *priv = _g_paste_daemon_get_instance_private (self);
    GPasteDaemonMethods methods = {
        priv->connection,
        priv->history,
        priv->settings,
        priv->clipboards_manager
    };
    GVariant *answer = NULL;
    GError *error = NULL;
    g_autofree GPasteDBusError *err = NULL;

    if (g_paste_str_equal (method_name, G_PASTE_DAEMON_ABOUT))
        g_paste_util_activate_ui ("about", NULL);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_ADD))
        g_paste_daemon_methods_add (&methods, parameters, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_ADD_FILE))
        g_paste_daemon_methods_add_file (&methods, parameters, &error, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_ADD_PASSWORD))
        g_paste_daemon_methods_add_password (&methods, parameters, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_BACKUP_HISTORY))
        g_paste_daemon_methods_backup_history (&methods, parameters, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_DELETE))
        g_paste_daemon_methods_delete (&methods, parameters, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_DELETE_HISTORY))
        g_paste_daemon_methods_delete_history (&methods, parameters, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_DELETE_PASSWORD))
        g_paste_daemon_methods_delete_password (&methods, parameters, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_EMPTY_HISTORY))
        g_paste_daemon_methods_empty_history (&methods, parameters);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_GET_ELEMENT))
        answer = g_paste_daemon_methods_get_element (&methods, parameters, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_GET_ELEMENT_AT_INDEX))
        answer = g_paste_daemon_methods_get_element_at_index (&methods, parameters, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_GET_ELEMENT_KIND))
        answer = g_paste_daemon_methods_get_element_kind (&methods, parameters, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_GET_ELEMENTS))
        answer = g_paste_daemon_methods_get_elements (&methods, parameters, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_GET_HISTORY))
        answer = g_paste_daemon_methods_get_history (&methods);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_GET_HISTORY_NAME))
        answer = g_paste_daemon_methods_get_history_name (&methods);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_GET_HISTORY_SIZE))
        answer = g_paste_daemon_methods_get_history_size (&methods, parameters);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_GET_RAW_ELEMENT))
        answer = g_paste_daemon_methods_get_raw_element (&methods, parameters, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_GET_RAW_HISTORY))
        answer = g_paste_daemon_methods_get_raw_history (&methods);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_LIST_HISTORIES))
        answer = g_paste_daemon_methods_list_histories (&methods, &error);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_MERGE))
        g_paste_daemon_methods_merge (&methods, parameters, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_ON_EXTENSION_STATE_CHANGED))
        g_paste_daemon_methods_on_extension_state_changed (&methods, parameters);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_REEXECUTE))
        g_paste_daemon_reexecute (self);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_RENAME_PASSWORD))
        g_paste_daemon_methods_rename_password (&methods, parameters, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_REPLACE))
        g_paste_daemon_methods_replace (&methods, parameters, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_SEARCH))
        answer = g_paste_daemon_methods_search (&methods, parameters, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_SELECT))
        g_paste_daemon_methods_select (&methods, parameters, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_SET_PASSWORD))
        g_paste_daemon_methods_set_password (&methods, parameters, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_SHOW_HISTORY))
        g_paste_daemon_show_history (self, &error);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_SWITCH_HISTORY))
        g_paste_daemon_methods_switch_history (&methods, parameters, &err);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_TRACK))
        g_paste_daemon_methods_track (&methods, parameters);
    else if (g_paste_str_equal (method_name, G_PASTE_DAEMON_UPLOAD))
        _g_paste_daemon_upload (self, parameters, &err);

    if (error)
        g_dbus_method_invocation_take_error (invocation, error);
    else if (err)
        g_dbus_method_invocation_return_dbus_error (invocation, err->name, err->msg);
    else
        g_dbus_method_invocation_return_value (invocation, answer);
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
    const GPasteDaemonPrivate *priv = _g_paste_daemon_get_instance_private (G_PASTE_DAEMON (user_data));

    if (g_paste_str_equal (property_name, G_PASTE_DAEMON_PROP_ACTIVE))
        return g_variant_new_boolean (g_paste_settings_get_track_changes (priv->settings));
    else if (g_paste_str_equal (property_name, G_PASTE_DAEMON_PROP_VERSION))
        return g_variant_new_string (VERSION);

    return NULL;
}

static void
g_paste_daemon_unregister_object (gpointer user_data)
{
    g_autoptr (GPasteDaemon) self = G_PASTE_DAEMON (user_data);
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);

    g_signal_group_set_target (priv->settings_signals, NULL);
    g_signal_group_set_target (priv->history_signals, NULL);
    g_signal_group_set_target (priv->screensaver_signals, NULL);

    priv->registered = FALSE;
}

static void
g_paste_daemon_on_history_update (GPasteDaemon      *self,
                                  GPasteUpdateAction action,
                                  GPasteUpdateTarget target,
                                  guint64            position,
                                  gpointer           user_data G_GNUC_UNUSED)
{
    g_paste_daemon_update (self, action, target, position);
}

static void
g_paste_daemon_on_history_switch (GPasteDaemonPrivate *priv,
                                  const gchar         *name,
                                  gpointer             user_data G_GNUC_UNUSED)
{
    GVariant *variant = g_variant_new_string (name);

    G_PASTE_SEND_DBUS_SIGNAL_WITH_DATA (SWITCH_HISTORY, variant);
}

static void
g_paste_daemon_on_screensaver_active_changed (GPasteDaemonPrivate *priv,
                                              gboolean             active,
                                              gpointer             user_data G_GNUC_UNUSED)
{
    if (!priv->registered)
        return;

    /* The deactivate signal is always sent, but not the activate one */
    /* We always do the activate action, so that the deactivate one works anyways */
    {
        g_autoptr (GPasteItem) item = g_paste_text_item_new ("");
        /* will always return TRUE */
        g_paste_clipboards_manager_select (priv->clipboards_manager, item);
    }

    if (!active)
    {
        g_autoptr (GPasteItem) item = g_paste_history_dup (priv->history, 0);

        if (item)
        {
            if (!g_paste_clipboards_manager_select (priv->clipboards_manager, item))
                g_paste_history_remove (priv->history, 0);
        }
    }
}

static void
_g_paste_daemon_changed (gpointer data)
{
    GPasteDaemon *self = G_PASTE_DAEMON (data);

    g_paste_daemon_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0);
}

static void
g_paste_daemon_dispose (GObject *object)
{
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (G_PASTE_DAEMON (object));

    if (priv->connection)
    {
        g_dbus_connection_unregister_object (priv->connection, priv->id_on_bus);
        g_clear_object (&priv->connection);
    }

    g_clear_object (&priv->history_signals);
    g_clear_object (&priv->settings_signals);
    g_clear_object (&priv->screensaver_signals);
    g_clear_object (&priv->history);
    g_clear_object (&priv->settings);
    g_clear_object (&priv->clipboards_manager);
    g_clear_object (&priv->keybinder);
    g_clear_object (&priv->screensaver);
    g_clear_pointer (&priv->g_paste_daemon_dbus_info, g_dbus_node_info_unref);

    G_OBJECT_CLASS (g_paste_daemon_parent_class)->dispose (object);
}

static gboolean
g_paste_daemon_register_on_connection (GPasteBusObject *self,
                                       GDBusConnection *connection,
                                       GError         **error)
{
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (G_PASTE_DAEMON (self));

    g_clear_object (&priv->connection);
    priv->connection = g_object_ref (connection);

    priv->id_on_bus = g_dbus_connection_register_object (connection,
                                                         G_PASTE_DAEMON_OBJECT_PATH,
                                                         priv->g_paste_daemon_dbus_info->interfaces[0],
                                                         &priv->g_paste_daemon_dbus_vtable,
                                                         g_object_ref (self),
                                                         g_paste_daemon_unregister_object,
                                                         error);

    if (!priv->id_on_bus)
        return FALSE;

    g_signal_group_set_target (priv->settings_signals, priv->settings);
    g_signal_group_set_target (priv->history_signals, priv->history);
    priv->registered = TRUE;

    g_source_set_name_by_id (g_timeout_add_seconds_once (1, _g_paste_daemon_changed, self), "[GPaste] Startup - changed");

    return TRUE;
}

static void
g_paste_daemon_class_init (GPasteDaemonClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_daemon_dispose;
    G_PASTE_BUS_OBJECT_CLASS (klass)->register_on_connection = g_paste_daemon_register_on_connection;

    /**
     * GPasteDaemon::reexecute-self:
     * @gpaste_daemon: the object on which the signal was emitted
     *
     * The "reexecute-self" signal is emitted when the daemon is about
     * to reexecute itself into a new freshly spawned daemon
     */
    signals[REEXECUTE_SELF] = g_signal_new ("reexecute-self",
                                            G_PASTE_TYPE_DAEMON,
                                            G_SIGNAL_RUN_LAST,
                                            0, /* class offset */
                                            NULL, /* accumulator */
                                            NULL, /* accumulator data */
                                            g_cclosure_marshal_VOID__VOID,
                                            G_TYPE_NONE,
                                            0);
}

static void
on_screensaver_client_ready (GObject      *source_object G_GNUC_UNUSED,
                             GAsyncResult *res,
                             gpointer      user_data)
{
    GPasteDaemonPrivate *priv = user_data;
    g_autoptr (GError) error = NULL;
    GPasteScreensaverClient *screensaver = priv->screensaver = g_paste_screensaver_client_new_finish (res, &error);

    if (error)
    {
        g_warning ("Couldn't watch screensaver state: %s", error->message);
        g_clear_object (&priv->screensaver);
    }
    else if (screensaver)
        g_signal_group_set_target (priv->screensaver_signals, screensaver);
}

static void
on_portal_client_ready (GObject      *source_object G_GNUC_UNUSED,
                        GAsyncResult *res,
                        gpointer      user_data)
{
    GPasteDaemon *self = user_data;
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteGlobalShortcutClient) portal_client = g_paste_global_shortcut_client_new_finish (res, &error);

    if (error)
    {
        g_warning ("Couldn't connect to the GlobalShortcuts portal, keyboard shortcuts won't work: %s", error->message);
        return;
    }

    priv->keybinder = g_paste_keybinder_new (priv->settings, portal_client);
    g_paste_daemon_activate_default_keybindings (self);
}

static void
g_paste_daemon_init (GPasteDaemon *self)
{
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);
    GDBusInterfaceVTable *vtable = &priv->g_paste_daemon_dbus_vtable;

    priv->id_on_bus = 0;
    g_autoptr (GError) error = NULL;
    priv->g_paste_daemon_dbus_info = g_dbus_node_info_new_for_xml (G_PASTE_DAEMON_INTERFACE,
                                                                   &error);
    g_assert_no_error (error);

    vtable->method_call = g_paste_daemon_dbus_method_call;
    vtable->get_property = g_paste_daemon_dbus_get_property;
    vtable->set_property = NULL;

    /* The settings, history, clipboards manager and providers are wired up in
     * g_paste_daemon_new () from the caller-provided GPasteSettings. */

    priv->history_signals = g_signal_group_new (G_PASTE_TYPE_HISTORY);
    g_signal_group_connect_swapped (priv->history_signals,
                                    "update",
                                    G_CALLBACK (g_paste_daemon_on_history_update),
                                    self);
    g_signal_group_connect_swapped (priv->history_signals,
                                    "switch",
                                    G_CALLBACK (g_paste_daemon_on_history_switch),
                                    priv);

    priv->settings_signals = g_signal_group_new (G_PASTE_TYPE_SETTINGS);
    g_signal_group_connect_swapped (priv->settings_signals,
                                    "track",
                                    G_CALLBACK (g_paste_daemon_tracking),
                                    self);

    priv->screensaver_signals = g_signal_group_new (G_PASTE_TYPE_SCREENSAVER_CLIENT);
    g_signal_group_connect_swapped (priv->screensaver_signals,
                                    "active-changed",
                                    G_CALLBACK (g_paste_daemon_on_screensaver_active_changed),
                                    priv);

    g_paste_screensaver_client_new (on_screensaver_client_ready, priv);
    g_paste_global_shortcut_client_new (on_portal_client_ready, self);
}

/**
 * g_paste_daemon_new:
 * @settings: (transfer none): the #GPasteSettings shared by the whole daemon
 * @clipboard: (transfer none): the clipboard selection provider
 * @primary: (transfer none): the primary selection provider
 *
 * Create a new instance of #GPasteDaemon, handing the (backend-specific)
 * clipboard providers to the clipboards manager. The caller picks the backend
 * by choosing which #GPasteClipboardProvider implementations it builds, and
 * owns the single @settings instance threaded through the daemon (history,
 * clipboards manager and providers all share it).
 *
 * Returns: a newly allocated #GPasteDaemon
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteDaemon *
g_paste_daemon_new (GPasteSettings          *settings,
                    GPasteClipboardProvider *clipboard,
                    GPasteClipboardProvider *primary)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (_G_PASTE_IS_CLIPBOARD_PROVIDER (clipboard), NULL);
    g_return_val_if_fail (_G_PASTE_IS_CLIPBOARD_PROVIDER (primary), NULL);

    GPasteDaemon *self = G_PASTE_DAEMON (g_object_new (G_PASTE_TYPE_DAEMON, NULL));
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);

    priv->settings = g_object_ref (settings);
    GPasteHistory *history = priv->history = g_paste_history_new (settings);
    GPasteClipboardsManager *clipboards_manager = priv->clipboards_manager = g_paste_clipboards_manager_new (history, settings);

    g_paste_clipboards_manager_add_clipboard (clipboards_manager, clipboard);
    g_paste_clipboards_manager_add_clipboard (clipboards_manager, primary);
    g_paste_clipboards_manager_activate (clipboards_manager);

    g_paste_history_load_async (history, NULL);

    return self;
}

/**
 * g_paste_daemon_new_gdk:
 * @settings: a #GPasteSettings instance
 *
 * Create a new instance of #GPasteDaemon driving the GDK clipboard backend.
 *
 * Returns: a newly allocated #GPasteDaemon
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteDaemon *
g_paste_daemon_new_gdk (GPasteSettings *settings)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);

    g_autoptr (GPasteClipboardProvider) clipboard = g_paste_clipboard_gdk_new_clipboard (settings);
    g_autoptr (GPasteClipboardProvider) primary = g_paste_clipboard_gdk_new_primary (settings);

    return g_paste_daemon_new (settings, clipboard, primary);
}
