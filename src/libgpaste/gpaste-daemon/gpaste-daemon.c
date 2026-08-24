// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-clipboards-manager.h>
#include <gpaste-daemon/gpaste-daemon-methods.h>
#include <gpaste-daemon/gpaste-daemon.h>
#include <gpaste-daemon/gpaste-history.h>
#include <gpaste-daemon/gpaste-keybinder.h>
#include <gpaste-daemon/gpaste-screensaver-client.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-global-shortcut-client.h>

#ifdef G_PASTE_ENABLE_GNOME_SHELL
#include <gpaste-daemon/gpaste-clipboard-meta.h>
#endif

#include <string.h>

struct _GPasteDaemon
{
    GPasteBusObject parent_instance;

    GPasteDaemon3           *skeleton;
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

static guint signals[LAST_SIGNAL] = { 0 };

/* The context the free-standing method handlers work on. Declared as a local so
 * it always mirrors the daemon's current state, and named the same way at every
 * call site so the handlers below read as one shape. */
#define G_PASTE_DAEMON_METHODS(self)    \
    {                                   \
        (self)->skeleton,               \
        (self)->history,                \
        (self)->settings,               \
        (self)->clipboards_manager      \
    }

/****************/
/* DBus Signals */
/****************/

/* The enums travel as their own values: the nicks are the storage backends'
 * spelling of an enum, and nothing on the wire has to share it. */
static void
g_paste_daemon_update (GPasteDaemon      *self,
                       GPasteUpdateAction action,
                       GPasteUpdateTarget target,
                       const gchar       *uuid,
                       guint64            position)
{
    g_paste_daemon3_emit_raw_update (self->skeleton, action, target, uuid, position);
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

    g_paste_daemon3_emit_raw_show_history (self->skeleton);
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
 * Same as the ReportExtensionState D-Bus method, for a host that runs the
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
    g_paste_daemon3_set_active (self->skeleton, g_paste_settings_get_track_changes (settings));
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
g_paste_daemon_change_passphrase (GPasteDaemon *self,
                                  GError      **error)
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
on_wgetpaste_done (GObject      *source_object,
                   GAsyncResult *res,
                   gpointer      user_data)
{
    /* The ref g_subprocess_new () handed us; the async call holds its own for as
     * long as it needs one. */
    g_autoptr (GSubprocess) upload = G_SUBPROCESS (source_object);
    g_autoptr (GTask) task = user_data;
    g_autofree gchar *url = NULL;
    GError *error = NULL;

    if (!g_subprocess_communicate_utf8_finish (upload, res, &url, NULL, &error))
    {
        g_task_return_error (task, error);
        return;
    }

    /* wgetpaste answers the url on stdout, with the newline it ended the line
     * with; anything else means it did not upload. */
    if (url)
        g_strstrip (url);

    if (!url || !*url)
    {
        g_task_return_new_error (task, G_PASTE_ERROR, G_PASTE_ERROR_FAILED, "The pastebin service answered no url.");
        return;
    }

    g_task_return_pointer (task, g_steal_pointer (&url), g_free);
}

/**
 * g_paste_daemon_upload:
 * @self: (transfer none): the #GPasteDaemon
 * @uuid: the uuid of the item to upload, or "" (or %NULL) for the current one
 * @callback: (nullable): a #GAsyncReadyCallback to call once the upload is done
 * @user_data: (nullable): the data to pass to @callback
 *
 * Upload an item to a pastebin service.
 *
 * The upload is what this waits for, not merely the spawning of the tool that
 * performs it: the url only exists once it has finished, and an upload that
 * failed is what @callback is told about. What to do with the url is the
 * caller's business -- nothing is added to the history here.
 */
G_PASTE_VISIBLE void
g_paste_daemon_upload (GPasteDaemon       *self,
                       const gchar        *uuid,
                       GAsyncReadyCallback callback,
                       gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_DAEMON (self));

    g_autoptr (GTask) task = g_task_new (self, NULL, callback, user_data);

    g_task_set_source_tag (task, g_paste_daemon_upload);

    /* No uuid means the current item, which is what the keyboard shortcut
     * uploads. It travels as an empty string rather than as nothing, a D-Bus
     * string never being absent. */
    GPasteItem *item = (uuid && *uuid) ? g_paste_history_get_by_uuid (self->history, uuid) : g_paste_history_get (self->history, 0);

    if (!item)
    {
        g_task_return_new_error (task, G_PASTE_ERROR, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.");
        return;
    }

    g_autoptr (GError) error = NULL;
    GSubprocess *upload = g_subprocess_new (G_SUBPROCESS_FLAGS_STDIN_PIPE|G_SUBPROCESS_FLAGS_STDOUT_PIPE, &error, "wgetpaste", NULL);

    if (!upload)
    {
        g_task_return_new_error (task, G_PASTE_ERROR, G_PASTE_ERROR_FAILED, "Failed to spawn wgetpaste: %s", error->message);
        return;
    }

    g_subprocess_communicate_utf8_async (upload,
                                         g_paste_item_get_value (item),
                                         NULL, /* cancellable */
                                         on_wgetpaste_done,
                                         g_steal_pointer (&task));
}

/**
 * g_paste_daemon_upload_finish:
 * @self: (transfer none): the #GPasteDaemon
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Finish an upload started with g_paste_daemon_upload().
 *
 * Errors are in %G_PASTE_ERROR (%G_PASTE_ERROR_NOT_FOUND for a uuid matching
 * nothing, %G_PASTE_ERROR_FAILED when the tool could not be run or answered no
 * url) or whatever running it reported.
 *
 * Returns: (transfer full) (nullable): the url the item was uploaded to
 */
G_PASTE_VISIBLE gchar *
g_paste_daemon_upload_finish (GPasteDaemon *self,
                              GAsyncResult *result,
                              GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_DAEMON (self), NULL);
    g_return_val_if_fail (g_task_is_valid (result, self), NULL);

    return g_task_propagate_pointer (G_TASK (result), error);
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

    g_autofree gchar *uuid = g_paste_history_set_password (history, g_paste_item_get_uuid (first), NULL);
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
on_keybinding_upload_done (GObject      *source_object,
                           GAsyncResult *res,
                           gpointer      user_data G_GNUC_UNUSED)
{
    GPasteDaemon *self = G_PASTE_DAEMON (source_object);
    g_autoptr (GError) error = NULL;
    g_autofree gchar *url = g_paste_daemon_upload_finish (self, res, &error);

    if (!url)
    {
        g_warning ("Upload failed: %s", error->message);
        return;
    }

    /* A shortcut has nobody to hand a url back to, so the history is where it
     * goes: putting it on the clipboard is the whole of what pressing the
     * shortcut does. The D-Bus method answers its caller instead, and adds
     * nothing. */
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);
    g_autoptr (GError) add_error = NULL;
    g_autofree gchar *uuid = g_paste_daemon_methods_do_add (&methods, url, strlen (url), &add_error);

    if (!uuid)
        g_warning ("Failed to add the uploaded url: %s", add_error->message);
}

static void
keybinding_upload (GPasteKeybinding *self G_GNUC_UNUSED,
                   gpointer          data)
{
    g_paste_daemon_upload (data, NULL, on_keybinding_upload_done, NULL);
}

static void
g_paste_daemon_activate_default_keybindings (GPasteDaemon *self)
{
    GPasteKeybinder *keybinder = self->keybinder;
    GPasteHistory *history = self->history;
    GPasteClipboardsManager *clipboards_manager = self->clipboards_manager;
    /* What to run for each shortcut, and what to run it on. Everything the user
     * ever sees -- the description the shortcut UIs show, the short label the
     * portal lists it under -- comes from the shared table instead, so a
     * shortcut added there and forgotten here warns rather than silently doing
     * nothing. */
    const struct
    {
        const gchar           *key;
        GPasteKeybindingGetter getter;
        GPasteKeybindingFunc   callback;
        gpointer               user_data;
    } handlers[] = {
        { G_PASTE_MAKE_PASSWORD_SETTING,             g_paste_settings_get_make_password,             keybinding_make_password,             history            },
        { G_PASTE_POP_SETTING,                       g_paste_settings_get_pop,                       keybinding_pop,                       history            },
        { G_PASTE_SHOW_HISTORY_SETTING,              g_paste_settings_get_show_history,              keybinding_show_history,              self               },
        { G_PASTE_SYNC_CLIPBOARD_TO_PRIMARY_SETTING, g_paste_settings_get_sync_clipboard_to_primary, keybinding_sync_clipboard_to_primary, clipboards_manager },
        { G_PASTE_SYNC_PRIMARY_TO_CLIPBOARD_SETTING, g_paste_settings_get_sync_primary_to_clipboard, keybinding_sync_primary_to_clipboard, clipboards_manager },
        { G_PASTE_LAUNCH_UI_SETTING,                 g_paste_settings_get_launch_ui,                 keybinding_launch_ui,                 NULL               },
        { G_PASTE_UPLOAD_SETTING,                    g_paste_settings_get_upload,                    keybinding_upload,                    self               },
    };

    gsize n_shortcuts = 0;
    const GPasteKeybindingInfo *shortcuts = g_paste_keybindings (&n_shortcuts);

    for (gsize i = 0; i < n_shortcuts; ++i)
    {
        const GPasteKeybindingInfo *shortcut = &shortcuts[i];
        gsize h;

        for (h = 0; h < G_N_ELEMENTS (handlers); ++h)
        {
            if (g_paste_str_equal (handlers[h].key, shortcut->key))
                break;
        }

        if (h == G_N_ELEMENTS (handlers))
        {
            g_warning ("Nothing to run for the \"%s\" keyboard shortcut", shortcut->key);
            continue;
        }

        g_paste_keybinder_add_keybinding (keybinder,
                                          g_paste_keybinding_new (shortcut->key,
                                                                  _(shortcut->portal_label),
                                                                  handlers[h].getter,
                                                                  handlers[h].callback,
                                                                  handlers[h].user_data));
    }

    g_paste_keybinder_activate_all (keybinder);
}

/****************/
/* DBus Methods */
/****************/

/* One handler per method on the generated skeleton, connected swapped so @self
 * comes first. Each one runs the method and answers the invocation: the error it
 * set, or the reply its matching completion builds. Returning %TRUE says the
 * method was handled, which is what stops the skeleton from replying itself.
 *
 * They come in four shapes, by whether the method answers anything and whether
 * it can fail. PARAMS is what the interface adds after the invocation and ARGS
 * the matching arguments to forward; both are parenthesized so the preprocessor
 * takes each as one macro argument, and ARGLIST supplies the comma joining them
 * -- disappearing when the list is empty.
 *
 * A handler's signature is not checked against the vfunc it is connected to, so
 * concentrating them here is also what keeps that hazard in four places rather
 * than thirty-three: compare these against struct _GPasteDaemon3Iface by hand
 * when a method changes. */
#define ARGLIST(...) , ##__VA_ARGS__

#define G_PASTE_DAEMON_ANSWER(complete_call)                                            \
    do {                                                                                \
        if (error)                                                                      \
            g_dbus_method_invocation_take_error (invocation, g_steal_pointer (&error)); \
        else                                                                            \
            complete_call;                                                              \
        return TRUE;                                                                    \
    } while (FALSE)

#define G_PASTE_DAEMON_HANDLER_HEAD(name, PARAMS)                                     \
    static gboolean                                                                   \
    g_paste_daemon_handle_##name (GPasteDaemon          *self,                        \
                                  GDBusMethodInvocation *invocation ARGLIST PARAMS)

/* Answers nothing, cannot fail. */
#define G_PASTE_DAEMON_HANDLER(name, PARAMS, ARGS)                                    \
    G_PASTE_DAEMON_HANDLER_HEAD (name, PARAMS)                                        \
    {                                                                                 \
        const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);            \
                                                                                      \
        g_paste_daemon_methods_##name (&methods ARGLIST ARGS);                        \
        g_paste_daemon3_complete_##name (self->skeleton, invocation);                 \
                                                                                      \
        return TRUE;                                                                  \
    }

/* Answers nothing, may fail. */
#define G_PASTE_DAEMON_HANDLER_ERR(name, PARAMS, ARGS)                                        \
    G_PASTE_DAEMON_HANDLER_HEAD (name, PARAMS)                                                \
    {                                                                                         \
        const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);                    \
        g_autoptr (GError) error = NULL;                                                      \
                                                                                              \
        g_paste_daemon_methods_##name (&methods ARGLIST ARGS, &error);                        \
                                                                                              \
        G_PASTE_DAEMON_ANSWER (g_paste_daemon3_complete_##name (self->skeleton, invocation)); \
    }

/* Answers something, cannot fail. */
#define G_PASTE_DAEMON_HANDLER_RET(name, PARAMS, ARGS)                                           \
    G_PASTE_DAEMON_HANDLER_HEAD (name, PARAMS)                                                   \
    {                                                                                            \
        const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);                       \
                                                                                                 \
        g_paste_daemon3_complete_##name (self->skeleton,                                         \
                                         invocation,                                             \
                                         g_paste_daemon_methods_##name (&methods ARGLIST ARGS)); \
                                                                                                 \
        return TRUE;                                                                             \
    }

/* Answers something, may fail: @decl declares what the method hands back and
 * @value is what the completion sends. */
#define G_PASTE_DAEMON_HANDLER_RET_ERR(name, decl, value, PARAMS, ARGS)                              \
    G_PASTE_DAEMON_HANDLER_HEAD (name, PARAMS)                                                       \
    {                                                                                                \
        const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);                           \
        g_autoptr (GError) error = NULL;                                                             \
        decl = g_paste_daemon_methods_##name (&methods ARGLIST ARGS, &error);                        \
                                                                                                     \
        G_PASTE_DAEMON_ANSWER (g_paste_daemon3_complete_##name (self->skeleton, invocation, value)); \
    }

G_PASTE_DAEMON_HANDLER_RET_ERR (add_file,
                                g_autofree gchar *uuid, uuid,
                                (const gchar *path), (path))

G_PASTE_DAEMON_HANDLER_RET_ERR (add_text,
                                g_autofree gchar *uuid, uuid,
                                (const gchar *text), (text))

G_PASTE_DAEMON_HANDLER_RET_ERR (add_password,
                                g_autofree gchar *uuid, uuid,
                                (const gchar *name, const gchar *password), (name, password))

G_PASTE_DAEMON_HANDLER_ERR (backup_history, (const gchar *history, const gchar *backup), (history, backup))

static gboolean
g_paste_daemon_handle_change_passphrase (GPasteDaemon          *self,
                                         GDBusMethodInvocation *invocation)
{
    g_autoptr (GError) error = NULL;

    g_paste_daemon_change_passphrase (self, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon3_complete_change_passphrase (self->skeleton, invocation));
}

G_PASTE_DAEMON_HANDLER_ERR (delete_item, (const gchar *uuid), (uuid))

G_PASTE_DAEMON_HANDLER_ERR (delete_history, (const gchar *name), (name))

G_PASTE_DAEMON_HANDLER_ERR (delete_password, (const gchar *name), (name))

G_PASTE_DAEMON_HANDLER_ERR (empty_history, (const gchar *name), (name))

G_PASTE_DAEMON_HANDLER_RET (get_favourites, (), ())

G_PASTE_DAEMON_HANDLER_RET (get_history, (), ())

G_PASTE_DAEMON_HANDLER_RET (get_history_size, (), ())

G_PASTE_DAEMON_HANDLER_RET_ERR (get_image,
                                GVariant *image, image,
                                (const gchar *uuid), (uuid))

G_PASTE_DAEMON_HANDLER_RET_ERR (get_item,
                                GVariant *item, item,
                                (const gchar *uuid), (uuid))

G_PASTE_DAEMON_HANDLER_RET_ERR (get_item_at_index,
                                GVariant *item, item,
                                (guint64 index), (index))

G_PASTE_DAEMON_HANDLER_RET_ERR (get_items,
                                GVariant *items, items,
                                (const gchar * const *uuids), (uuids))

G_PASTE_DAEMON_HANDLER_RET_ERR (get_uris,
                                g_auto (GStrv) uris, (const gchar * const *) uris,
                                (const gchar *uuid), (uuid))

G_PASTE_DAEMON_HANDLER_RET_ERR (list_histories,
                                GVariant *histories, histories,
                                (), ())

G_PASTE_DAEMON_HANDLER_RET_ERR (make_password,
                                g_autofree gchar *new_uuid, new_uuid,
                                (const gchar *uuid, const gchar *name), (uuid, name))

G_PASTE_DAEMON_HANDLER_RET_ERR (merge,
                                g_autofree gchar *uuid, uuid,
                                (const gchar *decoration, const gchar *separator, const gchar * const *uuids), (decoration, separator, uuids))

static gboolean
g_paste_daemon_handle_report_extension_state (GPasteDaemon          *self,
                                              GDBusMethodInvocation *invocation,
                                              gboolean               active)
{
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (self);

    g_paste_daemon_methods_extension_state_changed (&methods, active);
    g_paste_daemon3_complete_report_extension_state (self->skeleton, invocation);

    return TRUE;
}

static gboolean
g_paste_daemon_handle_reexecute (GPasteDaemon          *self,
                                 GDBusMethodInvocation *invocation)
{
    g_paste_daemon_reexecute (self);
    g_paste_daemon3_complete_reexecute (self->skeleton, invocation);

    return TRUE;
}

G_PASTE_DAEMON_HANDLER_ERR (rename_password, (const gchar *old_name, const gchar *new_name), (old_name, new_name))

G_PASTE_DAEMON_HANDLER_RET_ERR (replace,
                                g_autofree gchar *new_uuid, new_uuid,
                                (const gchar *uuid, const gchar *contents), (uuid, contents))

G_PASTE_DAEMON_HANDLER_RET_ERR (search,
                                GVariant *results, results,
                                (const gchar *query), (query))

G_PASTE_DAEMON_HANDLER_ERR (select, (const gchar *uuid), (uuid))

G_PASTE_DAEMON_HANDLER (set_active, (gboolean active), (active))

G_PASTE_DAEMON_HANDLER_ERR (set_favourite, (const gchar *uuid, gboolean favourite), (uuid, favourite))

static gboolean
g_paste_daemon_handle_show_history (GPasteDaemon          *self,
                                    GDBusMethodInvocation *invocation)
{
    g_autoptr (GError) error = NULL;

    g_paste_daemon_show_history (self, &error);

    G_PASTE_DAEMON_ANSWER (g_paste_daemon3_complete_show_history (self->skeleton, invocation));
}

G_PASTE_DAEMON_HANDLER_ERR (switch_history, (const gchar *name), (name))

/* The one method that cannot answer from its handler: the url exists only once
 * wgetpaste has finished, so the invocation is carried along and answered from
 * the callback. */
static void
on_upload_done (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
    GPasteDaemon *self = G_PASTE_DAEMON (source_object);
    GDBusMethodInvocation *invocation = user_data;
    g_autoptr (GError) error = NULL;
    g_autofree gchar *url = g_paste_daemon_upload_finish (self, res, &error);

    if (!url)
        g_dbus_method_invocation_take_error (invocation, g_steal_pointer (&error));
    else
        g_paste_daemon3_complete_upload (self->skeleton, invocation, url);
}

static gboolean
g_paste_daemon_handle_upload (GPasteDaemon          *self,
                              GDBusMethodInvocation *invocation,
                              const gchar           *uuid)
{
    g_paste_daemon_upload (self, uuid, on_upload_done, invocation);

    return TRUE;
}

#undef ARGLIST

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
        { "handle-add-file",                    G_CALLBACK (g_paste_daemon_handle_add_file)                    },
        { "handle-add-password",                G_CALLBACK (g_paste_daemon_handle_add_password)                },
        { "handle-add-text",                    G_CALLBACK (g_paste_daemon_handle_add_text)                    },
        { "handle-backup-history",              G_CALLBACK (g_paste_daemon_handle_backup_history)              },
        { "handle-change-passphrase",           G_CALLBACK (g_paste_daemon_handle_change_passphrase)           },
        { "handle-delete-history",              G_CALLBACK (g_paste_daemon_handle_delete_history)              },
        { "handle-delete-item",                 G_CALLBACK (g_paste_daemon_handle_delete_item)                 },
        { "handle-delete-password",             G_CALLBACK (g_paste_daemon_handle_delete_password)             },
        { "handle-empty-history",               G_CALLBACK (g_paste_daemon_handle_empty_history)               },
        { "handle-get-favourites",              G_CALLBACK (g_paste_daemon_handle_get_favourites)              },
        { "handle-get-history",                 G_CALLBACK (g_paste_daemon_handle_get_history)                 },
        { "handle-get-history-size",            G_CALLBACK (g_paste_daemon_handle_get_history_size)            },
        { "handle-get-image",                   G_CALLBACK (g_paste_daemon_handle_get_image)                   },
        { "handle-get-item",                    G_CALLBACK (g_paste_daemon_handle_get_item)                    },
        { "handle-get-item-at-index",           G_CALLBACK (g_paste_daemon_handle_get_item_at_index)           },
        { "handle-get-items",                   G_CALLBACK (g_paste_daemon_handle_get_items)                   },
        { "handle-get-uris",                    G_CALLBACK (g_paste_daemon_handle_get_uris)                    },
        { "handle-list-histories",              G_CALLBACK (g_paste_daemon_handle_list_histories)              },
        { "handle-make-password",               G_CALLBACK (g_paste_daemon_handle_make_password)               },
        { "handle-merge",                       G_CALLBACK (g_paste_daemon_handle_merge)                       },
        { "handle-reexecute",                   G_CALLBACK (g_paste_daemon_handle_reexecute)                   },
        { "handle-rename-password",             G_CALLBACK (g_paste_daemon_handle_rename_password)             },
        { "handle-replace",                     G_CALLBACK (g_paste_daemon_handle_replace)                     },
        { "handle-report-extension-state",      G_CALLBACK (g_paste_daemon_handle_report_extension_state)      },
        { "handle-search",                      G_CALLBACK (g_paste_daemon_handle_search)                      },
        { "handle-select",                      G_CALLBACK (g_paste_daemon_handle_select)                      },
        { "handle-set-active",                  G_CALLBACK (g_paste_daemon_handle_set_active)                  },
        { "handle-set-favourite",               G_CALLBACK (g_paste_daemon_handle_set_favourite)               },
        { "handle-show-history",                G_CALLBACK (g_paste_daemon_handle_show_history)                },
        { "handle-switch-history",              G_CALLBACK (g_paste_daemon_handle_switch_history)              },
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
                                  const gchar       *uuid,
                                  guint64            position,
                                  gpointer           user_data G_GNUC_UNUSED)
{
    g_paste_daemon_update (self, action, target, uuid, position);
}

/* Which history is in use is state, so it is a property: the skeleton turns the
 * assignment into the PropertiesChanged a client listens for. */
static void
g_paste_daemon_on_history_switch (GPasteDaemon *self,
                                  const gchar  *name,
                                  gpointer      user_data G_GNUC_UNUSED)
{
    g_paste_daemon3_set_history (self->skeleton, name);
}

static void
g_paste_daemon_on_screensaver_active_changed (GPasteDaemon            *self,
                                              GParamSpec              *pspec G_GNUC_UNUSED,
                                              GPasteScreensaverClient *screensaver)
{
    if (!self->registered)
        return;

    gboolean active = g_paste_screensaver_client_is_active (screensaver);

    /* Blank the selection on both transitions: nothing of the history sits in
     * the clipboard while the screen is locked, and unlocking puts the head
     * item back over that blank. An empty text item is always accepted, hence
     * the unchecked return value here (unlike the one below). */
    {
        g_autoptr (GPasteItem) item = g_paste_text_item_new ("");

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

    g_paste_daemon_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, "", 0);
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

    /* Seed "Active" from the setting: set_target does not emit notify, so the
     * handler above only ever runs on a later change, and the property would
     * read FALSE until the user toggled tracking. Here rather than in init() so
     * it is the state as of the moment the interface goes on the bus. */
    g_paste_daemon_tracking (daemon, NULL, daemon->settings);

    /* And "History" likewise, for the same reason one level down: the property
     * is written from the "switch" signal, which loading the history at startup
     * does not emit -- only a later change of history does. Unseeded it would
     * read "", and a client that sized or emptied "the current history" would
     * name a history called "" instead. The setting is where the daemon's own
     * history took its name from (g_paste_history_load_async (history, NULL)). */
    g_paste_daemon3_set_history (daemon->skeleton, g_paste_settings_get_history_name (daemon->settings));

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
     * follows the track-changes setting, and is seeded from it when the
     * interface is exported (see g_paste_daemon_tracking()). */
    self->skeleton = G_PASTE_DAEMON3 (g_paste_daemon3_skeleton_new ());
    g_paste_daemon3_set_version (self->skeleton, VERSION);

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
