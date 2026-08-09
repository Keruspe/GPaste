// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-clipboards-manager.h>
#include <gpaste-daemon/gpaste-daemon-methods.h>
#include <gpaste-daemon/gpaste-daemon.h>
#include <gpaste-daemon/gpaste-history.h>
#include <gpaste-daemon/gpaste-keybinder.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-global-shortcut-client.h>

#ifdef G_PASTE_ENABLE_GNOME_SHELL
#include <gpaste-daemon/gpaste-clipboard-meta.h>
#endif

#include <string.h>

struct _GPasteDaemon
{
    GPasteBusObject parent_instance;

    GPasteDaemon2           *skeleton;
    gboolean                 registered;

    GPasteHistory           *history;
    GPasteSettings          *settings;
    GPasteClipboardsManager *clipboards_manager;
    GPasteKeybinder         *keybinder;
    GPasteScreensaverClient *screensaver;

    GSignalGroup            *history_signals;
    GSignalGroup            *settings_signals;
    GSignalGroup            *screensaver_signals;
};

G_PASTE_DEFINE_TYPE (Daemon, daemon, G_PASTE_TYPE_BUS_OBJECT)

enum
{
    REEXECUTE_SELF,
    CHANGE_PASSPHRASE,

    LAST_SIGNAL
};

static guint64 signals[LAST_SIGNAL] = { 0 };

/* The context the free-standing method handlers work on. Declared as a local so
 * it always mirrors the daemon's current state, and named the same way at every
 * call site so the handlers below read as one shape. */
#define G_PASTE_DAEMON_METHODS(self)   \
    {                                   \
        (self)->skeleton,               \
        (self)->history,                \
        (self)->settings,               \
        (self)->clipboards_manager      \
    }

/****************/
/* DBus Signals */
/****************/

static void
g_paste_daemon_update (GPasteDaemon      *self,
                       GPasteUpdateAction action,
                       GPasteUpdateTarget target,
                       guint64            position)
{
    g_paste_daemon2_emit_raw_update (self->skeleton,
                                     g_enum_get_value (g_type_class_peek (G_PASTE_TYPE_UPDATE_ACTION), action)->value_nick,
                                     g_enum_get_value (g_type_class_peek (G_PASTE_TYPE_UPDATE_TARGET), target)->value_nick,
                                     position);
}

/**
 * g_paste_daemon_show_history:
 * @self: (transfer none): the #GPasteDaemon
 * @error: return location for a #GError, or %NULL
 *
 * Emit the signal to show history
 *
 * This never fails: handing the signal to the exported interface is all it does,
 * and that reports nothing back. @error is left untouched, and is only still
 * here because callers pass one.
 */
G_PASTE_VISIBLE void
g_paste_daemon_show_history (GPasteDaemon *self,
                             GError      **error G_GNUC_UNUSED)
{
    g_return_if_fail (G_PASTE_IS_DAEMON (self));

    g_paste_daemon2_emit_raw_show_history (self->skeleton);
}

/**
 * g_paste_daemon_flush:
 * @self: (transfer none): the #GPasteDaemon
 *
 * Persist every pending history change and wait for it to hit the disk, then
 * stop recording. Meant for an exit/handover path (the daemon losing its name to
 * a takeover, or being told to stop): once this returns the on-disk history is up
 * to date, so a successor daemon can load it without losing the last changes.
 */
G_PASTE_VISIBLE void
g_paste_daemon_flush (GPasteDaemon *self)
{
    g_return_if_fail (G_PASTE_IS_DAEMON (self));

    g_paste_history_flush (self->history);
}

/**
 * g_paste_daemon_resume:
 * @self: (transfer none): the #GPasteDaemon
 *
 * Undo a previous g_paste_daemon_flush(): resume recording. Meant for a handover
 * that did not happen (e.g. a re-exec whose exec failed), so the daemon that is
 * still running keeps persisting the history.
 */
G_PASTE_VISIBLE void
g_paste_daemon_resume (GPasteDaemon *self)
{
    g_return_if_fail (G_PASTE_IS_DAEMON (self));

    g_paste_history_resume (self->history);
}

/**
 * g_paste_daemon_reload_storage:
 * @self: (transfer none): the #GPasteDaemon
 *
 * Reload the history from its storage backend, rebuilding that backend from the
 * (possibly changed) "storage-backend" setting. Meant to be called after an
 * on-demand storage migration has rewritten the store on disk, so the live
 * daemon picks up the new backend without a full re-exec. Assumes the caller
 * flushed first (see g_paste_daemon_flush()); used by the gnome-shell host,
 * where the daemon cannot re-exec itself.
 */
G_PASTE_VISIBLE void
g_paste_daemon_reload_storage (GPasteDaemon *self)
{
    g_return_if_fail (G_PASTE_IS_DAEMON (self));

    g_paste_history_reload_backend (self->history);
}

/**
 * g_paste_daemon_extension_state_changed:
 * @self: (transfer none): the #GPasteDaemon
 * @state: whether the gnome-shell extension is there
 *
 * Same as the OnExtensionStateChanged D-Bus method, for a host that runs the
 * daemon in its own process: the gnome-shell extension being disabled is also
 * what tears its daemon down, so the D-Bus call it would otherwise make is
 * dispatched — if at all — against a name it has already released. Applied
 * straight away here instead, so the "track-extension-state" policy is not lost
 * on the one path that has no daemon left to reach afterwards.
 */
G_PASTE_VISIBLE void
g_paste_daemon_extension_state_changed (GPasteDaemon *self,
                                        gboolean      state)
{
    g_return_if_fail (G_PASTE_IS_DAEMON (self));

    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);

    g_paste_daemon_methods_extension_state_changed (&methods, state);
}

/* Connected swapped to notify::track-changes, so @settings is the emitter: the
 * new state is read back from it rather than carried by the signal. */
static void
g_paste_daemon_tracking (GPasteDaemon   *self,
                         GParamSpec     *pspec G_GNUC_UNUSED,
                         GPasteSettings *settings)
{
    g_paste_daemon2_set_active (self->skeleton, g_paste_settings_get_track_changes (settings));
}

/********************/
/* Daemon controls  */
/********************/

static void
g_paste_daemon_reexecute (GPasteDaemon *self)
{
    g_paste_clipboards_manager_store (self->clipboards_manager);

    g_signal_emit (self,
                   signals[REEXECUTE_SELF],
                   0, /* detail */
                   NULL);
}

/* The prompts a passphrase change needs are the host's job (each has its own
 * GPastePrompt backend), so this only asks. The new passphrase never travels
 * over the bus: it is typed into the host's prompt.
 *
 * Refused outright when there is no passphrase to change, so a host never stops
 * recording and raises a dialog only to be told there was nothing to do. */
static void
g_paste_daemon_change_passphrase (GPasteDaemon  *self,
                                  GError       **error)
{
    G_PASTE_DBUS_ASSERT (g_paste_storage_is_encrypted (g_paste_settings_get_storage_backend (self->settings)),
                         G_PASTE_ERROR_NOT_ENCRYPTED,
                         "The history is not encrypted; there is no passphrase to change.");

    g_signal_emit (self,
                   signals[CHANGE_PASSPHRASE],
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
    GPasteDaemon *self = user_data;
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);

    g_autoptr (GError) error = NULL;
    if (!g_subprocess_communicate_utf8_finish (upload, res, &url, NULL, &error))
        g_warning ("Upload failed: %s", error->message);

    if (url)
    {
        g_autoptr (GError) add_error = NULL;

        g_paste_daemon_methods_do_add (&methods, url, strlen (url), &add_error);
        if (add_error)
            g_warning ("Failed to add the uploaded url: %s", add_error->message);
    }
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
    g_return_val_if_fail (G_PASTE_IS_DAEMON (self), FALSE);

    GPasteItem *item = (uuid) ? g_paste_history_get_by_uuid (self->history, uuid) : g_paste_history_get (self->history, 0);

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
                                         self);
    return TRUE;
}

/****************/
/* Keybindings  */
/****************/

static void
keybinding_make_password (GPasteKeybinding *self G_GNUC_UNUSED,
                          gpointer          data)
{
    GPasteHistory *history = data;
    GPasteItem *first = g_paste_history_get (history, 0);

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
    GPasteKeybinder *keybinder = self->keybinder;
    GPasteHistory *history = self->history;
    GPasteClipboardsManager *clipboards_manager = self->clipboards_manager;
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

/* One handler per method on the generated skeleton, connected swapped so @self
 * comes first. Each one runs the method and answers the invocation: the error it
 * set, or the reply its matching completion builds. Returning %TRUE says the
 * method was handled, which is what stops the skeleton from replying itself. */

#define G_PASTE_DAEMON_ANSWER(complete_call)                                          \
    do {                                                                              \
        if (error)                                                                    \
            g_dbus_method_invocation_take_error (invocation, g_steal_pointer (&error)); \
        else                                                                          \
            complete_call;                                                            \
        return TRUE;                                                                  \
    } while (FALSE)

static gboolean
g_paste_daemon_handle_about (GPasteDaemon          *self,
                             GDBusMethodInvocation *invocation)
{
    g_paste_util_activate_ui ("about", NULL);
    g_paste_daemon2_complete_about (self->skeleton, invocation);

    return TRUE;
}

static gboolean
g_paste_daemon_handle_add (GPasteDaemon          *self,
                           GDBusMethodInvocation *invocation,
                           const gchar           *text)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_add (&methods, text, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_add (self->skeleton, invocation));
}

static gboolean
g_paste_daemon_handle_add_file (GPasteDaemon          *self,
                                GDBusMethodInvocation *invocation,
                                const gchar           *file)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_add_file (&methods, file, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_add_file (self->skeleton, invocation));
}

static gboolean
g_paste_daemon_handle_add_password (GPasteDaemon          *self,
                                    GDBusMethodInvocation *invocation,
                                    const gchar           *name,
                                    const gchar           *password)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_add_password (&methods, name, password, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_add_password (self->skeleton, invocation));
}

static gboolean
g_paste_daemon_handle_backup_history (GPasteDaemon          *self,
                                      GDBusMethodInvocation *invocation,
                                      const gchar           *history,
                                      const gchar           *backup)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_backup_history (&methods, history, backup, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_backup_history (self->skeleton, invocation));
}

static gboolean
g_paste_daemon_handle_change_passphrase (GPasteDaemon          *self,
                                         GDBusMethodInvocation *invocation)
{
    g_autoptr (GError) error = NULL;

    g_paste_daemon_change_passphrase (self, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_change_passphrase (self->skeleton, invocation));
}

static gboolean
g_paste_daemon_handle_delete (GPasteDaemon          *self,
                              GDBusMethodInvocation *invocation,
                              const gchar           *uuid)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_delete (&methods, uuid, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_delete (self->skeleton, invocation));
}

static gboolean
g_paste_daemon_handle_delete_history (GPasteDaemon          *self,
                                      GDBusMethodInvocation *invocation,
                                      const gchar           *name)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_delete_history (&methods, name, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_delete_history (self->skeleton, invocation));
}

static gboolean
g_paste_daemon_handle_delete_password (GPasteDaemon          *self,
                                       GDBusMethodInvocation *invocation,
                                       const gchar           *name)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_delete_password (&methods, name, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_delete_password (self->skeleton, invocation));
}

static gboolean
g_paste_daemon_handle_empty_history (GPasteDaemon          *self,
                                     GDBusMethodInvocation *invocation,
                                     const gchar           *name)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);

    g_paste_daemon_methods_empty_history (&methods, name);
    g_paste_daemon2_complete_empty_history (self->skeleton, invocation);

    return TRUE;
}

static gboolean
g_paste_daemon_handle_get_element (GPasteDaemon          *self,
                                   GDBusMethodInvocation *invocation,
                                   const gchar           *uuid)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;
    const gchar *value = g_paste_daemon_methods_get_element (&methods, uuid, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_get_element (self->skeleton, invocation, value));
}

static gboolean
g_paste_daemon_handle_get_element_at_index (GPasteDaemon          *self,
                                            GDBusMethodInvocation *invocation,
                                            guint64                index)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;
    const gchar *uuid = NULL, *value = NULL;

    g_paste_daemon_methods_get_element_at_index (&methods, index, &uuid, &value, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_get_element_at_index (self->skeleton, invocation, uuid, value));
}

static gboolean
g_paste_daemon_handle_get_element_kind (GPasteDaemon          *self,
                                        GDBusMethodInvocation *invocation,
                                        const gchar           *uuid)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;
    const gchar *kind = g_paste_daemon_methods_get_element_kind (&methods, uuid, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_get_element_kind (self->skeleton, invocation, kind));
}

static gboolean
g_paste_daemon_handle_get_elements (GPasteDaemon          *self,
                                    GDBusMethodInvocation *invocation,
                                    const gchar * const   *uuids)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;
    GVariant *elements = g_paste_daemon_methods_get_elements (&methods, uuids, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_get_elements (self->skeleton, invocation, elements));
}

static gboolean
g_paste_daemon_handle_get_history (GPasteDaemon          *self,
                                   GDBusMethodInvocation *invocation)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);

    g_paste_daemon2_complete_get_history (self->skeleton, invocation, g_paste_daemon_methods_get_history (&methods));

    return TRUE;
}

static gboolean
g_paste_daemon_handle_get_history_name (GPasteDaemon          *self,
                                        GDBusMethodInvocation *invocation)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);

    g_paste_daemon2_complete_get_history_name (self->skeleton, invocation, g_paste_daemon_methods_get_history_name (&methods));

    return TRUE;
}

static gboolean
g_paste_daemon_handle_get_history_size (GPasteDaemon          *self,
                                        GDBusMethodInvocation *invocation,
                                        const gchar           *name)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);

    g_paste_daemon2_complete_get_history_size (self->skeleton, invocation, g_paste_daemon_methods_get_history_size (&methods, name));

    return TRUE;
}

static gboolean
g_paste_daemon_handle_get_image (GPasteDaemon          *self,
                                 GDBusMethodInvocation *invocation,
                                 const gchar           *uuid)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;
    GVariant *image = g_paste_daemon_methods_get_image (&methods, uuid, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_get_image (self->skeleton, invocation, image));
}

static gboolean
g_paste_daemon_handle_get_raw_element (GPasteDaemon          *self,
                                       GDBusMethodInvocation *invocation,
                                       const gchar           *uuid)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;
    const gchar *value = g_paste_daemon_methods_get_raw_element (&methods, uuid, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_get_raw_element (self->skeleton, invocation, value));
}

static gboolean
g_paste_daemon_handle_get_raw_history (GPasteDaemon          *self,
                                       GDBusMethodInvocation *invocation)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);

    g_paste_daemon2_complete_get_raw_history (self->skeleton, invocation, g_paste_daemon_methods_get_raw_history (&methods));

    return TRUE;
}

static gboolean
g_paste_daemon_handle_list_histories (GPasteDaemon          *self,
                                      GDBusMethodInvocation *invocation)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;
    g_auto (GStrv) histories = g_paste_daemon_methods_list_histories (&methods, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_list_histories (self->skeleton, invocation, (const gchar * const *) histories));
}

static gboolean
g_paste_daemon_handle_merge (GPasteDaemon          *self,
                             GDBusMethodInvocation *invocation,
                             const gchar           *decoration,
                             const gchar           *separator,
                             const gchar * const   *uuids)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_merge (&methods, decoration, separator, uuids, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_merge (self->skeleton, invocation));
}

static gboolean
g_paste_daemon_handle_on_extension_state_changed (GPasteDaemon          *self,
                                                  GDBusMethodInvocation *invocation,
                                                  gboolean               extension_state)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);

    g_paste_daemon_methods_extension_state_changed (&methods, extension_state);
    g_paste_daemon2_complete_on_extension_state_changed (self->skeleton, invocation);

    return TRUE;
}

static gboolean
g_paste_daemon_handle_reexecute (GPasteDaemon          *self,
                                 GDBusMethodInvocation *invocation)
{
    g_paste_daemon_reexecute (self);
    g_paste_daemon2_complete_reexecute (self->skeleton, invocation);

    return TRUE;
}

static gboolean
g_paste_daemon_handle_rename_password (GPasteDaemon          *self,
                                       GDBusMethodInvocation *invocation,
                                       const gchar           *old_name,
                                       const gchar           *new_name)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_rename_password (&methods, old_name, new_name, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_rename_password (self->skeleton, invocation));
}

static gboolean
g_paste_daemon_handle_replace (GPasteDaemon          *self,
                               GDBusMethodInvocation *invocation,
                               const gchar           *uuid,
                               const gchar           *contents)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_replace (&methods, uuid, contents, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_replace (self->skeleton, invocation));
}

static gboolean
g_paste_daemon_handle_search (GPasteDaemon          *self,
                              GDBusMethodInvocation *invocation,
                              const gchar           *query)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;
    g_auto (GStrv) results = g_paste_daemon_methods_search (&methods, query, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_search (self->skeleton, invocation, (const gchar * const *) results));
}

static gboolean
g_paste_daemon_handle_select (GPasteDaemon          *self,
                              GDBusMethodInvocation *invocation,
                              const gchar           *uuid)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_select (&methods, uuid, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_select (self->skeleton, invocation));
}

static gboolean
g_paste_daemon_handle_set_password (GPasteDaemon          *self,
                                    GDBusMethodInvocation *invocation,
                                    const gchar           *uuid,
                                    const gchar           *name)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_set_password (&methods, uuid, name, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_set_password (self->skeleton, invocation));
}

static gboolean
g_paste_daemon_handle_show_history (GPasteDaemon          *self,
                                    GDBusMethodInvocation *invocation)
{
    g_autoptr (GError) error = NULL;

    g_paste_daemon_show_history (self, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_show_history (self->skeleton, invocation));
}

static gboolean
g_paste_daemon_handle_switch_history (GPasteDaemon          *self,
                                      GDBusMethodInvocation *invocation,
                                      const gchar           *name)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_switch_history (&methods, name, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon2_complete_switch_history (self->skeleton, invocation));
}

static gboolean
g_paste_daemon_handle_track (GPasteDaemon          *self,
                             GDBusMethodInvocation *invocation,
                             gboolean               tracking_state)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);

    g_paste_daemon_methods_track (&methods, tracking_state);
    g_paste_daemon2_complete_track (self->skeleton, invocation);

    return TRUE;
}

static gboolean
g_paste_daemon_handle_upload (GPasteDaemon          *self,
                              GDBusMethodInvocation *invocation,
                              const gchar           *uuid)
{
    if (g_paste_daemon_upload (self, uuid))
        g_paste_daemon2_complete_upload (self->skeleton, invocation);
    else
    {
        g_dbus_method_invocation_return_error_literal (invocation,
                                                      G_PASTE_ERROR,
                                                      G_PASTE_ERROR_NOT_FOUND,
                                                      "Provided uuid doesn't match any item.");
    }

    return TRUE;
}

/* Every method the interface declares, wired in one place so a method added to
 * the XML without a handler here shows up as a missing symbol rather than a
 * silent "not implemented" on the bus. Swapped, so @self leads. */
static void
g_paste_daemon_connect_handlers (GPasteDaemon *self)
{
    static const struct
    {
        const gchar *signal_name;
        GCallback    handler;
    } handlers[] = {
        { "handle-about",                       G_CALLBACK (g_paste_daemon_handle_about)                       },
        { "handle-add",                         G_CALLBACK (g_paste_daemon_handle_add)                         },
        { "handle-add-file",                    G_CALLBACK (g_paste_daemon_handle_add_file)                    },
        { "handle-add-password",                G_CALLBACK (g_paste_daemon_handle_add_password)                },
        { "handle-backup-history",              G_CALLBACK (g_paste_daemon_handle_backup_history)              },
        { "handle-change-passphrase",           G_CALLBACK (g_paste_daemon_handle_change_passphrase)           },
        { "handle-delete",                      G_CALLBACK (g_paste_daemon_handle_delete)                      },
        { "handle-delete-history",              G_CALLBACK (g_paste_daemon_handle_delete_history)              },
        { "handle-delete-password",             G_CALLBACK (g_paste_daemon_handle_delete_password)             },
        { "handle-empty-history",               G_CALLBACK (g_paste_daemon_handle_empty_history)               },
        { "handle-get-element",                 G_CALLBACK (g_paste_daemon_handle_get_element)                 },
        { "handle-get-element-at-index",        G_CALLBACK (g_paste_daemon_handle_get_element_at_index)        },
        { "handle-get-element-kind",            G_CALLBACK (g_paste_daemon_handle_get_element_kind)            },
        { "handle-get-elements",                G_CALLBACK (g_paste_daemon_handle_get_elements)                },
        { "handle-get-history",                 G_CALLBACK (g_paste_daemon_handle_get_history)                 },
        { "handle-get-history-name",            G_CALLBACK (g_paste_daemon_handle_get_history_name)            },
        { "handle-get-history-size",            G_CALLBACK (g_paste_daemon_handle_get_history_size)            },
        { "handle-get-image",                   G_CALLBACK (g_paste_daemon_handle_get_image)                   },
        { "handle-get-raw-element",             G_CALLBACK (g_paste_daemon_handle_get_raw_element)             },
        { "handle-get-raw-history",             G_CALLBACK (g_paste_daemon_handle_get_raw_history)             },
        { "handle-list-histories",              G_CALLBACK (g_paste_daemon_handle_list_histories)              },
        { "handle-merge",                       G_CALLBACK (g_paste_daemon_handle_merge)                       },
        { "handle-on-extension-state-changed",  G_CALLBACK (g_paste_daemon_handle_on_extension_state_changed)  },
        { "handle-reexecute",                   G_CALLBACK (g_paste_daemon_handle_reexecute)                   },
        { "handle-rename-password",             G_CALLBACK (g_paste_daemon_handle_rename_password)             },
        { "handle-replace",                     G_CALLBACK (g_paste_daemon_handle_replace)                     },
        { "handle-search",                      G_CALLBACK (g_paste_daemon_handle_search)                      },
        { "handle-select",                      G_CALLBACK (g_paste_daemon_handle_select)                      },
        { "handle-set-password",                G_CALLBACK (g_paste_daemon_handle_set_password)                },
        { "handle-show-history",                G_CALLBACK (g_paste_daemon_handle_show_history)                },
        { "handle-switch-history",              G_CALLBACK (g_paste_daemon_handle_switch_history)              },
        { "handle-track",                       G_CALLBACK (g_paste_daemon_handle_track)                       },
        { "handle-upload",                      G_CALLBACK (g_paste_daemon_handle_upload)                      },
    };

    for (guint64 i = 0; i < G_N_ELEMENTS (handlers); ++i)
        g_signal_connect_swapped (self->skeleton, handlers[i].signal_name, handlers[i].handler, self);
}

/* Drop the export (and the signal wiring that goes with it), so the object path
 * is free again for a successor daemon built on the same, still-alive
 * connection — which is exactly what the gnome-shell host does on re-enable. */
static void
g_paste_daemon_unregister_on_connection (GPasteBusObject *self)
{
    GPasteDaemon *daemon = G_PASTE_DAEMON (self);

    if (!daemon->registered)
        return;

    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (daemon->skeleton));

    g_signal_group_set_target (daemon->settings_signals, NULL);
    g_signal_group_set_target (daemon->history_signals, NULL);
    g_signal_group_set_target (daemon->screensaver_signals, NULL);

    daemon->registered = FALSE;
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
g_paste_daemon_on_history_switch (GPasteDaemon *self,
                                  const gchar         *name,
                                  gpointer             user_data G_GNUC_UNUSED)
{
    g_paste_daemon2_emit_raw_switch_history (self->skeleton, name);
}

static void
g_paste_daemon_on_screensaver_active_changed (GPasteDaemon            *self,
                                              GParamSpec              *pspec G_GNUC_UNUSED,
                                              GPasteScreensaverClient *screensaver)
{
    if (!self->registered)
        return;

    gboolean active = g_paste_screensaver_client_is_active (screensaver);

    /* The deactivate signal is always sent, but not the activate one */
    /* We always do the activate action, so that the deactivate one works anyways */
    {
        g_autoptr (GPasteItem) item = g_paste_text_item_new ("");
        /* will always return TRUE */
        g_paste_clipboards_manager_select (self->clipboards_manager, item);
    }

    if (!active)
    {
        g_autoptr (GPasteItem) item = g_paste_history_dup (self->history, 0);

        if (item)
        {
            if (!g_paste_clipboards_manager_select (self->clipboards_manager, item))
                g_paste_history_remove (self->history, 0);
        }
    }
}

static void
_g_paste_daemon_changed (gpointer data)
{
    GPasteDaemon *self = G_PASTE_DAEMON (data);

    g_paste_daemon_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0);
}

/* One-shot startup nudge: owns a ref (taken at the call site) so the daemon
 * cannot be finalized before the timeout fires, which would update freed memory. */
static void
_g_paste_daemon_changed_once (gpointer data)
{
    g_autoptr (GPasteDaemon) self = data;

    _g_paste_daemon_changed (self);
}

static void
g_paste_daemon_dispose (GObject *object)
{
    GPasteDaemon *self = G_PASTE_DAEMON (object);

    if (self->registered)
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self->skeleton));

    g_clear_object (&self->skeleton);
    g_clear_object (&self->history_signals);
    g_clear_object (&self->settings_signals);
    g_clear_object (&self->screensaver_signals);
    g_clear_object (&self->history);
    g_clear_object (&self->settings);
    g_clear_object (&self->clipboards_manager);
    g_clear_object (&self->keybinder);
    g_clear_object (&self->screensaver);

    G_OBJECT_CLASS (g_paste_daemon_parent_class)->dispose (object);
}

static gboolean
g_paste_daemon_register_on_connection (GPasteBusObject *self,
                                       GDBusConnection *connection,
                                       GError         **error)
{
    GPasteDaemon *daemon = G_PASTE_DAEMON (self);

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (daemon->skeleton),
                                           connection,
                                           G_PASTE_DAEMON_OBJECT_PATH,
                                           error))
    {
        return FALSE;
    }

    g_signal_group_set_target (daemon->settings_signals, daemon->settings);
    g_signal_group_set_target (daemon->history_signals, daemon->history);
    daemon->registered = TRUE;

    g_source_set_name_by_id (g_timeout_add_seconds_once (1, _g_paste_daemon_changed_once, g_object_ref (self)), "[GPaste] Startup - changed");

    return TRUE;
}

static void
g_paste_daemon_class_init (GPasteDaemonClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_daemon_dispose;
    G_PASTE_BUS_OBJECT_CLASS (klass)->register_on_connection = g_paste_daemon_register_on_connection;
    G_PASTE_BUS_OBJECT_CLASS (klass)->unregister_on_connection = g_paste_daemon_unregister_on_connection;

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

    /**
     * GPasteDaemon::change-passphrase:
     * @gpaste_daemon: the object on which the signal was emitted
     *
     * The "change-passphrase" signal is emitted when the passphrase of the
     * encrypted history should be changed. Like "reexecute-self", the work
     * belongs to whoever hosts the daemon, which is also who supplies the
     * #GPastePrompt the prompts are put to the user through.
     */
    signals[CHANGE_PASSPHRASE] = g_signal_new ("change-passphrase",
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
    g_autoptr (GPasteDaemon) self = user_data; /* ref taken at the call site */
    g_autoptr (GError) error = NULL;
    GPasteScreensaverClient *screensaver = self->screensaver = g_paste_screensaver_client_new_finish (res, &error);

    if (error)
    {
        g_warning ("Couldn't watch screensaver state: %s", error->message);
        g_clear_object (&self->screensaver);
    }
    else if (screensaver)
        g_signal_group_set_target (self->screensaver_signals, screensaver);
}

static void
on_portal_client_ready (GObject      *source_object G_GNUC_UNUSED,
                        GAsyncResult *res,
                        gpointer      user_data)
{
    g_autoptr (GPasteDaemon) self = user_data; /* ref taken at the call site */
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteGlobalShortcutClient) portal_client = g_paste_global_shortcut_client_new_finish (res, &error);

    if (error)
    {
        g_warning ("Couldn't connect to the GlobalShortcuts portal, keyboard shortcuts won't work: %s", error->message);
        return;
    }

    self->keybinder = g_paste_keybinder_new (self->settings, portal_client);
    g_paste_daemon_activate_default_keybindings (self);
}

static void
g_paste_daemon_init (GPasteDaemon *self)
{
    /* The skeleton owns the marshalling and the property store; the daemon owns
     * the skeleton. "Version" never changes, so it is set once here; "Active"
     * follows the track-changes setting (see g_paste_daemon_tracking()). */
    self->skeleton = G_PASTE_DAEMON2 (g_paste_daemon2_skeleton_new ());
    g_paste_daemon2_set_version (self->skeleton, VERSION);

    g_paste_daemon_connect_handlers (self);

    /* The settings, history, clipboards manager and providers are wired up in
     * g_paste_daemon_new () from the caller-provided GPasteSettings. */

    self->history_signals = g_signal_group_new (G_PASTE_TYPE_HISTORY);
    g_signal_group_connect_swapped (self->history_signals,
                                    "update",
                                    G_CALLBACK (g_paste_daemon_on_history_update),
                                    self);
    g_signal_group_connect_swapped (self->history_signals,
                                    "switch",
                                    G_CALLBACK (g_paste_daemon_on_history_switch),
                                    self);

    self->settings_signals = g_signal_group_new (G_PASTE_TYPE_SETTINGS);
    g_signal_group_connect_swapped (self->settings_signals,
                                    "notify::" G_PASTE_TRACK_CHANGES_SETTING,
                                    G_CALLBACK (g_paste_daemon_tracking),
                                    self);

    self->screensaver_signals = g_signal_group_new (G_PASTE_TYPE_SCREENSAVER_CLIENT);
    g_signal_group_connect_swapped (self->screensaver_signals,
                                    "notify::active",
                                    G_CALLBACK (g_paste_daemon_on_screensaver_active_changed),
                                    self);

    /* Hold a ref across each async call: the callback owns it (g_autoptr), so the
     * daemon cannot be finalized out from under the in-flight client creation. */
    g_paste_screensaver_client_new (on_screensaver_client_ready, g_object_ref (self));
    g_paste_global_shortcut_client_new (on_portal_client_ready, g_object_ref (self));
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
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD_PROVIDER (clipboard), NULL);
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD_PROVIDER (primary), NULL);

    GPasteDaemon *self = G_PASTE_DAEMON (g_object_new (G_PASTE_TYPE_DAEMON, NULL));

    self->settings = g_object_ref (settings);
    GPasteHistory *history = self->history = g_paste_history_new (settings);
    GPasteClipboardsManager *clipboards_manager = self->clipboards_manager = g_paste_clipboards_manager_new (history, settings);

    g_paste_clipboards_manager_add_clipboard (clipboards_manager, clipboard);
    g_paste_clipboards_manager_add_clipboard (clipboards_manager, primary);
    g_paste_clipboards_manager_activate (clipboards_manager);

    g_paste_history_load_async (history, NULL);

    return self;
}

#ifdef G_PASTE_ENABLE_GNOME_SHELL
/**
 * g_paste_daemon_new_meta:
 * @settings: a #GPasteSettings instance
 * @selection: the mutter MetaSelection (typed as #GObject to keep this API
 *             free of a libmutter dependency)
 *
 * Create a new instance of #GPasteDaemon driving the mutter clipboard backend,
 * for use from inside gnome-shell. The same @selection backs both the clipboard
 * and the primary provider.
 *
 * Returns: a newly allocated #GPasteDaemon
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteDaemon *
g_paste_daemon_new_meta (GPasteSettings *settings,
                         GObject        *selection)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (META_IS_SELECTION (selection), NULL);

    MetaSelection *meta_selection = META_SELECTION (selection);
    g_autoptr (GPasteClipboardProvider) clipboard = g_paste_clipboard_meta_new_clipboard (meta_selection, settings);
    g_autoptr (GPasteClipboardProvider) primary = g_paste_clipboard_meta_new_primary (meta_selection, settings);

    return g_paste_daemon_new (settings, clipboard, primary);
}
#endif
