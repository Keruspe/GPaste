// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-clipboards-manager.h>
#include <gpaste-daemon/gpaste-clipboard-provider-private.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-text-item.h>

/* A restart waits for classification, not indefinitely for a quiet owner. */
#ifndef G_PASTE_FORCED_EXPIRY_WAIT_TIMEOUT
#define G_PASTE_FORCED_EXPIRY_WAIT_TIMEOUT (60 * 1000)
#endif

typedef struct
{
    GPasteItem *item;
    guint       timeout;
    gint64      deadline;
    gboolean    changed;
} PendingPassword;

static void
pending_password_free (gpointer data)
{
    PendingPassword *pending = data;

    g_object_unref (pending->item);
    g_free (pending);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PendingPassword, pending_password_free)

typedef struct
{
    GPasteClipboardsManager *manager;
    GPasteClipboardProvider *clipboard;
    GSignalGroup            *signal_group;
    GSList                  *pending_refreshes;
    guint64                  change_serial;

    /* The countdown a password reaching this selection arms, and the item it was
     * armed for -- ref'd, since it may leave the history while still sitting
     * there, and its value is what says whether the selection still carries it.
     *
     * Per selection rather than per manager: a password on the primary and a
     * password on the clipboard are two exposures with two deadlines, and a
     * route that reaches one of them has nothing to say about the other's.
     *
     * @password_timeout is how long that countdown was armed for, which the item
     * alone no longer says once MakePassword has written a new one over it. */
    gboolean                 password_expired;
    guint                    password_timeout_id;
    guint                    password_timeout;
    GPasteItem              *password;
    /* Edits name items, not selections. Keep each candidate until the read
     * identifies its text; deadlines start at the edit, not at the reply. */
    GSList                  *pending_passwords;
    gboolean                 force_expiry;
} _Clipboard;

struct _GPasteClipboardsManager
{
    GObject parent_instance;

    GSList         *clipboards;
    GPasteHistory  *history;
    GSignalGroup   *history_signals;
    GPasteSettings *settings;
    guint64         change_serial;
    /* Tasks have no source object: reads, not waiters, keep the manager alive. */
    GSList         *expiry_tasks;
};

G_PASTE_DEFINE_TYPE (ClipboardsManager, clipboards_manager, G_TYPE_OBJECT)

static void g_paste_clipboards_manager_notify (GPasteClipboardProvider *clipboard, gpointer user_data);
static void g_paste_clipboards_manager_deselect_password (_Clipboard *clip);
static gboolean g_paste_clipboards_manager_selection_holds (GPasteClipboardProvider *clipboard, GPasteItem *item);
static void g_paste_clipboards_manager_on_password_timeout (gpointer user_data);
static void g_paste_clipboards_manager_ensure_not_empty (_Clipboard *clip);
static void g_paste_clipboards_manager_arm_password_at (_Clipboard *clip, GPasteItem *item, guint timeout, gint64 deadline, gboolean changed);

static gboolean
g_paste_clipboards_manager_expiry_pending (GPasteClipboardsManager *self)
{
    for (GSList *l = self->clipboards; l; l = l->next)
    {
        _Clipboard *clip = l->data;

        if (clip->force_expiry)
            return TRUE;
    }

    return FALSE;
}

typedef struct
{
    guint    timeout_id;
    gboolean timed_out;
} ExpiryWait;

static gboolean
return_expiry_on_idle (gpointer user_data)
{
    GTask *task = user_data;
    ExpiryWait *wait = g_task_get_task_data (task);

    if (wait->timed_out)
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "Password expiry could not identify the selections before its deadline.");
    else
        g_task_return_boolean (task, TRUE);
    return G_SOURCE_REMOVE;
}

static void
return_expiry_task (GTask    *task,
                    gboolean  timed_out)
{
    ExpiryWait *wait = g_task_get_task_data (task);

    g_clear_handle_id (&wait->timeout_id, g_source_remove);
    wait->timed_out = timed_out;
    /* GTask may invoke its callback inline on a later main-loop iteration.
     * Restart must wait until the triggering read has entered the history and
     * any Select/MakePassword handler has queued its D-Bus reply. The source
     * owns only the task and must deliver it even after manager disposal. */
    guint id = g_idle_add_full (G_PRIORITY_DEFAULT, return_expiry_on_idle, g_object_ref (task), g_object_unref);

    g_source_set_name_by_id (id, "[GPaste] password expiry completion");
}

static void
g_paste_clipboards_manager_return_expiry (GPasteClipboardsManager *self)
{
    g_autoslist (GTask) tasks = g_steal_pointer (&self->expiry_tasks);

    for (GSList *l = tasks; l; l = l->next)
        return_expiry_task (l->data, FALSE);
}

static void
g_paste_clipboards_manager_complete_expiry (GPasteClipboardsManager *self)
{
    if (!g_paste_clipboards_manager_expiry_pending (self))
        g_paste_clipboards_manager_return_expiry (self);
}

/* Forcing expiry is for whoever waits on it: once no waiter remains, no
 * selection is left forced. */
static void
g_paste_clipboards_manager_release_forced_expiry (GPasteClipboardsManager *self)
{
    if (self->expiry_tasks)
        return;

    for (GSList *l = self->clipboards; l; l = l->next)
        ((_Clipboard *) l->data)->force_expiry = FALSE;
}

static gboolean
g_paste_clipboards_manager_expiry_timed_out (gpointer user_data)
{
    g_autoptr (GPasteClipboardsManager) self = g_weak_ref_get (user_data);

    if (!self)
        return G_SOURCE_REMOVE;

    guint id = g_source_get_id (g_main_current_source ());

    for (GSList *l = self->expiry_tasks; l; l = l->next)
    {
        ExpiryWait *wait = g_task_get_task_data (l->data);

        if (wait->timeout_id != id)
            continue;

        g_autoptr (GTask) task = l->data;

        wait->timeout_id = 0;
        self->expiry_tasks = g_slist_delete_link (self->expiry_tasks, l);
        return_expiry_task (task, TRUE);
        break;
    }

    /* A later request keeps its own deadline and classification cleanup. */
    g_paste_clipboards_manager_release_forced_expiry (self);

    return G_SOURCE_REMOVE;
}

/* What an update in flight has to keep alive. A provider refs itself for the
 * whole read; nothing was holding this side up. The manager owns the _Clipboard
 * records and everything the reply then reaches for, so a ref on it is what
 * makes @clip still be there when that reply lands. @track says whether the item
 * it brings back is one to keep. */
typedef struct
{
    GPasteClipboardsManager *manager;
    _Clipboard              *clip;
    gboolean                 track;
    guint64                  change_serial;
} GPasteClipboardsManagerUpdateData;

static void
g_paste_clipboards_manager_update_data_free (GPasteClipboardsManagerUpdateData *data)
{
    g_clear_object (&data->manager);
    g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GPasteClipboardsManagerUpdateData, g_paste_clipboards_manager_update_data_free)

static GPasteClipboardsManagerUpdateData *
g_paste_clipboards_manager_update_data_new (_Clipboard *clip,
                                            gboolean    track)
{
    GPasteClipboardsManagerUpdateData *data = g_new0 (GPasteClipboardsManagerUpdateData, 1);

    data->manager = g_object_ref (clip->manager);
    data->clip = clip;
    data->track = track;
    data->change_serial = clip->change_serial;

    return data;
}

/* Maintenance preserves copy order but replaces the pending strip snapshot. */
static gboolean
g_paste_clipboards_manager_publish_item (_Clipboard *clip,
                                         GPasteItem *item)
{
    g_clear_slist (&clip->pending_refreshes, g_object_unref);
    return g_paste_clipboard_provider_select_item_full (clip->clipboard, item, FALSE);
}

static void
g_paste_clipboards_manager_publish_text (_Clipboard  *clip,
                                         const gchar *text)
{
    g_clear_slist (&clip->pending_refreshes, g_object_unref);
    g_paste_clipboard_provider_select_text_full (clip->clipboard, text, FALSE);
}

/* A strip can arrive before the selection's text is known. Only publish the
 * requested snapshot while it is still the history's item, and substitute it
 * for the read's item so that stale formats cannot enter the history again. */
static void
g_paste_clipboards_manager_finish_refresh (_Clipboard  *clip,
                                           GPasteItem **item)
{
    g_autoslist (GPasteItem) pending = g_steal_pointer (&clip->pending_refreshes);

    for (GSList *l = pending; l; l = l->next)
    {
        GPasteItem *plain = l->data;

        if (g_paste_history_get_by_uuid (clip->manager->history, g_paste_item_get_uuid (plain)) == plain &&
            g_paste_clipboards_manager_selection_holds (clip->clipboard, plain))
        {
            g_paste_clipboards_manager_publish_item (clip, plain);
            if (*item)
                g_set_object (item, plain);
            break;
        }
    }
}

/* Resolve timeout edits before expiry: an edit may cancel or shorten the
 * deadline that elapsed during classification. Neither may synchronize an
 * overdue password back onto another selection. */
static gboolean
g_paste_clipboards_manager_finish_expiry (_Clipboard *clip)
{
    if (g_paste_clipboard_provider_is_reading (clip->clipboard))
        return FALSE;

    g_autoslist (PendingPassword) pending = g_steal_pointer (&clip->pending_passwords);

    for (GSList *l = pending; l; l = l->next)
    {
        PendingPassword *change = l->data;

        if (g_paste_clipboards_manager_selection_holds (clip->clipboard, change->item))
        {
            g_paste_clipboards_manager_arm_password_at (clip, change->item, change->timeout, change->deadline, change->changed);
            break;
        }
    }

    if (!clip->password_expired && !clip->force_expiry)
        return FALSE;

    gboolean held = clip->password && g_paste_clipboards_manager_selection_holds (clip->clipboard, clip->password);

    clip->force_expiry = FALSE;
    g_paste_clipboards_manager_deselect_password (clip);
    g_paste_clipboards_manager_complete_expiry (clip->manager);

    return held;
}

static void
g_paste_clipboards_manager_finish_expiry_all (GPasteClipboardsManager *self)
{
    for (GSList *l = self->clipboards; l; l = l->next)
        g_paste_clipboards_manager_finish_expiry (l->data);
}

static void
g_paste_clipboards_manager_bootstrap_ready (GPasteClipboardProvider *clipboard G_GNUC_UNUSED,
                                            GPasteItem              *item,
                                            gboolean                 superseded,
                                            gpointer                 user_data)
{
    g_autoptr (GPasteClipboardsManagerUpdateData) data = user_data;
    /* The update callback owns the item it is handed (transfer full); at
     * bootstrap we only care about the selection not being empty, so whatever
     * was already in it is read and dropped rather than pushed to the history. */
    g_autoptr (GPasteItem) bootstrapped = item;

    /* A superseded read can finish expiry only when a publication, rather than
     * another read, replaced it. It must never restore the history itself. */
    if (g_paste_clipboards_manager_finish_expiry (data->clip) || superseded || !data->manager->history)
        return;

    g_paste_clipboards_manager_finish_refresh (data->clip, &bootstrapped);
    g_paste_clipboards_manager_ensure_not_empty (data->clip);
}

/* A strip applies to the copy being read when it was requested, not a later
 * copy of equal text. Local publications also end that copy without a read. */
static void
g_paste_clipboards_manager_published (GPasteClipboardProvider *clipboard G_GNUC_UNUSED,
                                      gboolean                 independent,
                                      gpointer                 user_data)
{
    _Clipboard *clip = user_data;

    if (independent)
    {
        g_clear_slist (&clip->pending_refreshes, g_object_unref);
        clip->change_serial = ++clip->manager->change_serial;
    }
}

/**
 * g_paste_clipboards_manager_add_clipboard:
 * @self: a #GPasteClipboardsManager instance
 * @clipboard: (transfer none): the GPasteClipboardProvider to add
 *
 * Add a #GPasteClipboardProvider to the #GPasteClipboardsManager
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_add_clipboard (GPasteClipboardsManager *self,
                                          GPasteClipboardProvider *clipboard)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));
    g_return_if_fail (G_PASTE_IS_CLIPBOARD_PROVIDER (clipboard));

    _Clipboard *clip = g_new0 (_Clipboard, 1);

    clip->manager = self;
    clip->clipboard = g_object_ref (clipboard);
    clip->signal_group = g_signal_group_new (G_PASTE_TYPE_CLIPBOARD_PROVIDER);
    g_signal_group_connect (clip->signal_group, "changed", G_CALLBACK (g_paste_clipboards_manager_notify), clip);
    g_signal_group_connect (clip->signal_group, "published", G_CALLBACK (g_paste_clipboards_manager_published), clip);

    self->clipboards = g_slist_prepend (self->clipboards, clip);
    g_paste_clipboard_provider_update (clipboard, g_paste_clipboards_manager_bootstrap_ready,
                                       g_paste_clipboards_manager_update_data_new (clip, FALSE));
}

/**
 * g_paste_clipboards_manager_sync_from_to:
 * @self: a #GPasteClipboardsManager instance
 * @from_clipboard: whether we sync from clipboard or to clipboard
 *
 * Sync a clipboard into another
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_sync_from_to (GPasteClipboardsManager *self,
                                         gboolean                 from_clipboard)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));

    GPasteClipboardProvider *_from = NULL;
    GPasteClipboardProvider *_to = NULL;

    g_debug ("clipboards-manager: sync_from_to");

    for (GSList *clipboard = self->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        _Clipboard *_clip = clipboard->data;
        GPasteClipboardProvider *clip = _clip->clipboard;

        if (g_paste_clipboard_provider_is_clipboard (clip) == from_clipboard)
            _from = clip;
        else
            _to = clip;
    }

    if (_from && _to)
        g_paste_clipboard_provider_sync_text (_from, _to);
}

static void
g_paste_clipboards_manager_notify_finish (_Clipboard  *clip,
                                          GPasteItem  *item,
                                          const gchar *synchronized_text,
                                          gboolean     something_in_clipboard,
                                          guint64      change_serial)
{
    GPasteClipboardsManager *self = clip->manager;
    GPasteHistory *history = self->history;

    g_debug ("clipboards-manager: notify finish");

    if (item)
        g_paste_history_add (history, item);

    if (!something_in_clipboard)
        g_paste_clipboards_manager_ensure_not_empty (clip);

    if (synchronized_text)
    {
        g_debug ("clipboards-manager: synchronizing clipboards");

        for (GSList *clipboard = self->clipboards; clipboard; clipboard = g_slist_next (clipboard))
        {
            _Clipboard *other = clipboard->data;

            /* Completion order is not copy order. Synchronization may retire an
             * older read, but must leave a newer external copy to conclude. */
            if (other == clip || other->change_serial > change_serial)
                continue;

            const gchar *text = g_paste_clipboard_provider_get_text (other->clipboard);

            if (!text || !g_paste_str_equal (text, synchronized_text))
            {
                other->change_serial = change_serial;
                g_paste_clipboards_manager_publish_text (other, synchronized_text);
            }
        }
    }
}

static void
g_paste_clipboards_manager_update_ready (GPasteClipboardProvider *clipboard,
                                         GPasteItem              *item,
                                         gboolean                 superseded,
                                         gpointer                 user_data)
{
    g_autoptr (GPasteClipboardsManagerUpdateData) data = user_data;
    _Clipboard *clip = data->clip;

    g_debug ("clipboards-manager: update ready");

    /* As in bootstrap_ready (): resolve any remaining expiry before dropping
     * stale results or results arriving after explicit disposal. */
    if (g_paste_clipboards_manager_finish_expiry (data->clip) || superseded || !data->manager->history)
    {
        g_clear_object (&item);
        return;
    }

    guint64 change_serial = data->change_serial;

    g_paste_clipboards_manager_finish_refresh (clip, &item);

    const gchar *synchronized_text = NULL;

    if (item && g_paste_clipboard_provider_get_text (clipboard) &&
        g_paste_settings_get_synchronize_clipboards (clip->manager->settings))
        synchronized_text = g_paste_clipboard_provider_get_text (clipboard);

    if (!data->track && item)
        g_clear_object (&item);

    gboolean something_in_clipboard = !!g_paste_clipboard_provider_get_text (clipboard) ||
                                      !!g_paste_clipboard_provider_get_image_checksum (clipboard);

    g_paste_clipboards_manager_notify_finish (clip, item, synchronized_text, something_in_clipboard, change_serial);
}

static void
g_paste_clipboards_manager_notify (GPasteClipboardProvider *clipboard,
                                   gpointer                 user_data)
{
    _Clipboard *clip = user_data;

    g_debug ("clipboards-manager: notify");

    g_clear_slist (&clip->pending_refreshes, g_object_unref);
    clip->change_serial = ++clip->manager->change_serial;

    GPasteSettings *settings = clip->manager->settings;
    gboolean track = (g_paste_settings_get_track_changes (settings) &&
                          (g_paste_clipboard_provider_is_clipboard (clipboard) ||             // We're not primary
                           g_paste_settings_get_primary_to_history (settings) ||     // Or we asked that primary affects clipboard
                           g_paste_settings_get_synchronize_clipboards (settings))); // Or primary and clipboards are synchronized hence primary will affect history through clipboard

    g_paste_clipboard_provider_update (clipboard,
                                       g_paste_clipboards_manager_update_ready,
                                       g_paste_clipboards_manager_update_data_new (clip, track));
}

/**
 * g_paste_clipboards_manager_activate:
 * @self: a #GPasteClipboardsManager instance
 *
 * Activate the #GPasteClipboardsManager
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_activate (GPasteClipboardsManager *self)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));

    for (GSList *clipboard = self->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        _Clipboard *clip = clipboard->data;

        g_signal_group_set_target (clip->signal_group, clip->clipboard);
    }
}

/* Whether @clipboard's selection still carries @item. A provider caches the
 * value it last published or read, so what it reports is what its selection
 * holds -- which is the question a password's countdown turns on, the history
 * having no say in what any one selection ended up with. */
static gboolean
g_paste_clipboards_manager_selection_holds (GPasteClipboardProvider *clipboard,
                                            GPasteItem              *item)
{
    const gchar *text = g_paste_clipboard_provider_get_text (clipboard);

    return text && g_paste_str_equal (text, g_paste_item_get_real_value (item));
}

/* Retiring the history's active password does not establish ownership of any
 * selection. Move the fallback without publishing it, even while reads are
 * pending, so a flush followed by a new daemon cannot restore the expired head. */
static void
g_paste_clipboards_manager_retire_password (GPasteClipboardsManager *self,
                                            GPasteItem              *password)
{
    if (!self->history || g_paste_history_get (self->history, 0) != password)
        return;

    const GPtrArray *history = g_paste_history_get_history (self->history);

    for (guint i = 1; i < history->len; ++i)
    {
        GPasteItem *item = g_ptr_array_index (history, i);

        if (!G_PASTE_IS_PASSWORD_ITEM (item))
        {
            /* add consumes this extra reference and moves the existing identity;
             * select would emit "selected" and overwrite unrelated selections. */
            g_paste_history_add (self->history, g_object_ref (item));
            return;
        }
    }
}

/* Arm the countdown @item needs, now that it has reached @clip's selection.
 *
 * Every route that *publishes an item* ends here -- select () for the history's
 * own signal and for the add path, ensure_not_empty () for a provider re-owning
 * a selection it found empty, rearm_password () for a timeout written under a
 * countdown already running. A password reaching a selection any other way sits
 * on it with nothing to take it off.
 *
 * The two writes that skip it are the ones taking a password back off:
 * deselect_password () putting the replacement or the empty string there, and
 * notify_finish () copying one selection's text onto the other. Neither can be
 * publishing a password -- the first picks the first item that is not one, the
 * second carries the text a provider just read -- so neither has a countdown to
 * arm. */
static void
g_paste_clipboards_manager_arm_password_at (_Clipboard *clip,
                                            GPasteItem *item,
                                            guint       timeout,
                                            gint64      deadline,
                                            gboolean    changed)
{
    /* The same password reaching this selection again is the exposure already
     * running out, not a new one: a selection re-taken every few seconds would
     * otherwise push its countdown back for as long as that kept happening.
     * Unless the duration itself moved, which is the one thing about an item
     * already armed for that MakePassword can change under it. */
    if (!changed && item == clip->password && timeout == clip->password_timeout)
        return;

    /* Whatever this selection's countdown was armed for is what @item has just
     * replaced on it, and no other selection's is any of this one's business. */
    g_clear_handle_id (&clip->password_timeout_id, g_source_remove);
    g_clear_object (&clip->password);
    clip->password_expired = FALSE;
    clip->password_timeout = 0;

    if (!timeout)
        return;

    /* Read once: a second reading past @deadline would make the remainder below
     * negative, which wraps to about 49 days as a guint. */
    gint64 now = g_get_monotonic_time ();

    clip->password = g_object_ref (item);
    clip->password_timeout = timeout;
    if (deadline <= now)
    {
        clip->password_expired = TRUE;
        return;
    }

    /* Armed for what is left until @deadline rather than for @timeout: an edit
     * resolved after a read kept the deadline it was made under, and a whole
     * duration handed out again there would be exposure the user did not ask
     * for. Milliseconds, since that remainder is not a round number of seconds. */
    clip->password_timeout_id = g_timeout_add_once ((deadline - now) / 1000,
                                                    g_paste_clipboards_manager_on_password_timeout,
                                                    clip);
    g_source_set_name_by_id (clip->password_timeout_id, "[GPaste] password timeout");
}

static guint
g_paste_clipboards_manager_password_timeout (GPasteItem *item)
{
    return G_PASTE_IS_PASSWORD_ITEM (item) ? g_paste_password_item_get_timeout (G_PASTE_PASSWORD_ITEM (item)) : 0;
}

static void
g_paste_clipboards_manager_arm_password (_Clipboard *clip,
                                         GPasteItem *item,
                                         gboolean    renew_expired)
{
    guint timeout = g_paste_clipboards_manager_password_timeout (item);

    /* An intentional selection after expiry starts a new exposure. Automatic
     * restoration and timeout edits still use the original edit's deadline. */
    g_autoslist (PendingPassword) pending = g_steal_pointer (&clip->pending_passwords);

    /* Publishing identifies the selection as surely as a read does. Keep the
     * edit's deadline when this publication resolves its pending read. */
    for (GSList *l = pending; l; l = l->next)
    {
        PendingPassword *change = l->data;

        if (change->item == item)
        {
            gint64 deadline = change->deadline;

            if (renew_expired && deadline <= g_get_monotonic_time ())
                deadline = g_get_monotonic_time () + (gint64) change->timeout * G_USEC_PER_SEC;
            g_paste_clipboards_manager_arm_password_at (clip, item, change->timeout, deadline,
                                                        change->changed || (renew_expired && clip->password_expired));
            return;
        }
    }

    g_paste_clipboards_manager_arm_password_at (clip, item, timeout, g_get_monotonic_time () + (gint64) timeout * G_USEC_PER_SEC, renew_expired && clip->password_expired);
}

/* A provider holding nothing re-owns its selection with the history's head,
 * which puts an item on it without going through select (): the countdown that
 * item may need is armed here instead. */
static void
g_paste_clipboards_manager_ensure_not_empty (_Clipboard *clip)
{
    GPasteItem *item = g_paste_clipboard_provider_ensure_not_empty (clip->clipboard, clip->manager->history);

    if (item)
    {
        g_paste_clipboards_manager_arm_password (clip, item, FALSE);
        g_paste_clipboards_manager_finish_expiry (clip);
    }
}

/* Take the password @clip's countdown was armed for back off its selection, if
 * that selection still carries it. One that has moved on holds what replaced it,
 * which is the user's and not ours to overwrite. History retirement is separate:
 * an expired head must not be restored when an unrelated selection falls empty. */
static void
g_paste_clipboards_manager_deselect_password (_Clipboard *clip)
{
    GPasteClipboardsManager *self = clip->manager;

    g_clear_handle_id (&clip->password_timeout_id, g_source_remove);

    if (clip->password)
        g_paste_clipboards_manager_retire_password (self, clip->password);

    if (clip->password && g_paste_clipboard_provider_is_reading (clip->clipboard))
    {
        clip->password_expired = TRUE;
        return;
    }

    clip->password_expired = FALSE;
    clip->password_timeout = 0;

    g_autoptr (GPasteItem) password = g_steal_pointer (&clip->password);

    if (!password)
        return;

    if (!g_paste_clipboards_manager_selection_holds (clip->clipboard, password))
        return;

    g_debug ("clipboards-manager: deselecting the password");

    /* A read can finish after explicit disposal. The selection still needs
     * clearing, but there is then no history to choose a replacement from. */
    const GPtrArray *history = (self->history) ? g_paste_history_get_history (self->history) : NULL;
    GPasteItem *replacement = NULL;

    for (guint i = 0; history && !replacement && i < history->len; ++i)
    {
        GPasteItem *item = g_ptr_array_index (history, i);

        if (!G_PASTE_IS_PASSWORD_ITEM (item))
            replacement = item;
    }

    /* With no item to put there the empty string goes on instead, through
     * select_text () rather than by dropping the content: a provider reports
     * emptiness by the *kind* it holds, so an empty string still counts as
     * something and ensure_not_empty () leaves it alone, where a genuinely empty
     * selection would have it re-select the history's head -- the very password
     * just taken off. */
    if (!replacement || !g_paste_clipboards_manager_publish_item (clip, replacement))
        g_paste_clipboards_manager_publish_text (clip, "");
}

static void
g_paste_clipboards_manager_on_password_timeout (gpointer user_data)
{
    _Clipboard *clip = user_data;

    /* This is the source firing, so it is spent: drop the id before anything can
     * reach for g_source_remove () on it. */
    clip->password_timeout_id = 0;
    g_paste_clipboards_manager_deselect_password (clip);
}

/**
 * g_paste_clipboards_manager_expire_password:
 * @self: a #GPasteClipboardsManager instance
 *
 * Request password expiry without waiting for the configured timeout
 *
 * Meant for the paths where there is no later to wait for: a daemon standing down
 * leaves the selections behind it, so a password still on one would stay there
 * for the rest of the session with nothing left to take it back off. A pending
 * read retains the cleanup until its selection is known, even across disposal.
 * A caller about to stop the main loop must await the async variant instead.
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_expire_password (GPasteClipboardsManager *self)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));

    for (GSList *clipboard = self->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        _Clipboard *clip = clipboard->data;

        clip->force_expiry = clip->password || clip->pending_passwords;
        if (clip->password)
            g_paste_clipboards_manager_retire_password (self, clip->password);
        for (GSList *l = clip->pending_passwords; l; l = l->next)
        {
            PendingPassword *pending = l->data;

            if (pending->timeout)
                g_paste_clipboards_manager_retire_password (self, pending->item);
        }
    }

    g_paste_clipboards_manager_finish_expiry_all (self);

    g_paste_clipboards_manager_complete_expiry (self);
}

/**
 * g_paste_clipboards_manager_expire_password_async:
 * @self: a #GPasteClipboardsManager
 * @callback: (scope async): called once pending password expiry is resolved
 * @user_data: closure data for @callback
 *
 * Expire passwords and wait for selections still being classified. Re-execution
 * must not discard the reads responsible for removing those passwords.
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_expire_password_async (GPasteClipboardsManager *self,
                                                  GAsyncReadyCallback      callback,
                                                  gpointer                 user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));

    /* No source object: g_task_new () would reference the very manager whose own
     * list holds the task, and a manager holding itself is one whose teardown --
     * the teardown this task is waiting for -- can never run. The callback is
     * handed the manager by whatever it kept a reference to instead. */
    g_autoptr (GTask) task = g_task_new (NULL, NULL, callback, user_data);

    g_task_set_static_name (task, "gpaste-password-expiry");
    g_task_set_source_tag (task, g_paste_clipboards_manager_expire_password_async);
    ExpiryWait *wait = g_new0 (ExpiryWait, 1);

    g_task_set_task_data (task, wait, g_free);
    wait->timeout_id = g_timeout_add_full (G_PRIORITY_DEFAULT, G_PASTE_FORCED_EXPIRY_WAIT_TIMEOUT,
                                           g_paste_clipboards_manager_expiry_timed_out,
                                           g_paste_weak_ref_new (self), g_paste_weak_ref_free);
    g_source_set_name_by_id (wait->timeout_id, "[GPaste] password expiry deadline");
    self->expiry_tasks = g_slist_prepend (self->expiry_tasks, g_steal_pointer (&task));
    g_paste_clipboards_manager_expire_password (self);
}

/**
 * g_paste_clipboards_manager_expire_password_finish:
 * @self: a #GPasteClipboardsManager
 * @result: the result of the expiry request
 * @error: return location for a #GError, or %NULL
 *
 * Revalidate expiry on the caller's turn: a task's completion can be queued
 * before another password is selected. Fails in the %G_IO_ERROR domain:
 * %G_IO_ERROR_PENDING asks the caller to await classification again, and
 * %G_IO_ERROR_TIMED_OUT, a deadline failure, must not lead to a restart.
 *
 * Returns: whether expiry is complete now, so a caller may immediately store
 *          and re-execute without yielding to another publication
 */
G_PASTE_VISIBLE gboolean
g_paste_clipboards_manager_expire_password_finish (GPasteClipboardsManager *self,
                                                   GAsyncResult            *result,
                                                   GError                 **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self), FALSE);
    g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);
    g_return_val_if_fail (g_async_result_is_tagged (result, g_paste_clipboards_manager_expire_password_async), FALSE);

    if (!g_task_propagate_boolean (G_TASK (result), error))
        return FALSE;

    g_paste_clipboards_manager_expire_password (self);
    if (g_paste_clipboards_manager_expiry_pending (self))
    {
        /* Revalidation belongs to this caller; another waiter may already be
         * awaiting the same read and still needs its forced cleanup. */
        g_paste_clipboards_manager_release_forced_expiry (self);
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PENDING, "A selection needs classification before password expiry can complete.");
        return FALSE;
    }

    return TRUE;
}

/**
 * g_paste_clipboards_manager_rearm_password:
 * @self: a #GPasteClipboardsManager instance
 * @item: the #GPasteItem whose timeout may have just changed
 *
 * Start @item's countdown over on every selection that currently holds it
 *
 * How long a password may stay on the clipboard is part of the item, so writing
 * it is what makes a running countdown wrong. Nothing is published: a selection
 * carrying @item already carries its value, and re-selecting a password whose
 * name or timeout is all that changed would put its cleartext back over whatever
 * the user has copied since. A selection that has moved on keeps whatever
 * countdown it has, @item not being its exposure to begin with -- and one whose
 * duration did not actually move keeps the countdown it is running, a rename
 * being no reason to hand the password more time on the clipboard.
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_rearm_password (GPasteClipboardsManager *self,
                                           GPasteItem              *item)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));
    g_return_if_fail (G_PASTE_IS_ITEM (item));

    for (GSList *clipboard = self->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        _Clipboard *clip = clipboard->data;

        if (g_paste_clipboard_provider_is_reading (clip->clipboard))
        {
            guint timeout = g_paste_clipboards_manager_password_timeout (item);
            PendingPassword *pending = NULL;

            for (GSList *l = clip->pending_passwords; l; l = l->next)
            {
                PendingPassword *candidate = l->data;

                if (candidate->item == item)
                    pending = candidate;
            }

            if (pending && pending->timeout == timeout)
                continue;

            if (!pending)
            {
                g_autoptr (PendingPassword) change = g_new0 (PendingPassword, 1);

                change->item = g_object_ref (item);
                change->changed = item != clip->password || timeout != clip->password_timeout;
                pending = g_steal_pointer (&change);
            }
            else
            {
                /* Returning to the armed duration is still an edit, whereas
                 * repeating the pending duration above is only a rename. */
                pending->changed = TRUE;
                clip->pending_passwords = g_slist_remove (clip->pending_passwords, pending);
            }

            pending->timeout = timeout;
            pending->deadline = g_get_monotonic_time () + (gint64) timeout * G_USEC_PER_SEC;
            clip->pending_passwords = g_slist_prepend (clip->pending_passwords, pending);
            continue;
        }

        if (!g_paste_clipboards_manager_selection_holds (clip->clipboard, item))
            continue;

        g_paste_clipboards_manager_arm_password (clip, item, FALSE);
    }

    g_paste_clipboards_manager_finish_expiry_all (self);
}

/**
 * g_paste_clipboards_manager_refresh_text:
 * @self: a #GPasteClipboardsManager instance
 * @item: the text item whose representations should be published
 *
 * Refresh selections still carrying @item's text, leaving unrelated selections
 * alone. A history position does not establish clipboard ownership: tracking
 * may be paused, and the clipboard and primary selection can differ.
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_refresh_text (GPasteClipboardsManager *self,
                                         GPasteItem              *item)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));
    g_return_if_fail (G_PASTE_IS_TEXT_ITEM (item));

    for (GSList *clipboard = self->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        _Clipboard *clip = clipboard->data;

        /* A hidden cache can be a pending read; its callback checks whether
         * the selection matches before publishing this snapshot. */
        if (g_paste_clipboard_provider_is_reading (clip->clipboard))
            clip->pending_refreshes = g_slist_prepend (clip->pending_refreshes, g_object_ref (item));
        else if (g_paste_clipboards_manager_selection_holds (clip->clipboard, item))
            g_paste_clipboards_manager_publish_item (clip, item);
    }
}

/**
 * g_paste_clipboards_manager_select:
 * @self: a #GPasteClipboardsManager instance
 * @item: the #GPasteItem to select
 *
 * Select a new #GPasteItem
 *
 * Returns: %FALSE if the item was invalid, %TRUE otherwise
 */
G_PASTE_VISIBLE gboolean
g_paste_clipboards_manager_select (GPasteClipboardsManager *self,
                                   GPasteItem              *item)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self), FALSE);
    g_return_val_if_fail (G_PASTE_IS_ITEM (item), FALSE);

    g_debug ("clipboards-manager: select");

    gboolean selected = TRUE;

    for (GSList *clipboard = self->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        _Clipboard *clip = clipboard->data;

        if (!g_paste_clipboard_provider_select_item (clip->clipboard, item))
        {
            g_debug ("clipboards-manager: item was invalid, deleting it");
            selected = FALSE;
            break;
        }

        /* Armed where the item reaching a selection is known rather than after
         * the loop: a provider that refuses it never took it, and the ones that
         * did are watched whatever a later one concludes -- or a secret sits on
         * their selections with nothing to take it off, under a uuid the caller
         * is about to drop from the history. */
        g_paste_clipboards_manager_arm_password (clip, item, TRUE);
    }

    /* Resolve forced expiry after every selection has been written, so a
     * publication cannot put a password back after another selection expires. */
    g_paste_clipboards_manager_finish_expiry_all (self);

    return selected;
}

/**
 * g_paste_clipboards_manager_store:
 * @self: a #GPasteClipboardsManager instance
 *
 * Store clipboards contents before exiting
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_store (GPasteClipboardsManager *self)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));

    g_debug ("clipboards-manager: store");

    for (GSList *clipboard = self->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        _Clipboard *clip = clipboard->data;

        g_paste_clipboard_provider_store (clip->clipboard);
    }
}

static void
on_item_selected (GPasteClipboardsManager *self,
                  GPasteItem              *item,
                  GPasteHistory           *history G_GNUC_UNUSED)
{
    if (!g_paste_clipboards_manager_select (self, item))
        g_paste_history_remove (self->history, 0);
}

static void
_clipboard_free (gpointer data)
{
    _Clipboard *clip = data;

    g_clear_handle_id (&clip->password_timeout_id, g_source_remove);
    g_clear_object (&clip->password);
    g_clear_slist (&clip->pending_passwords, pending_password_free);
    g_clear_slist (&clip->pending_refreshes, g_object_unref);
    g_clear_object (&clip->signal_group);
    g_object_unref (clip->clipboard);
    g_free (clip);
}

static void
g_paste_clipboards_manager_dispose (GObject *object)
{
    GPasteClipboardsManager *self = G_PASTE_CLIPBOARDS_MANAGER (object);

    /* Before anything is released: deselecting goes through the history and both
     * providers, and there is no point leaving a password on a selection nothing
     * will be watching. */
    g_paste_clipboards_manager_expire_password (self);

    /* Explicit disposal ends waiters; the outstanding reads retain cleanup. */
    g_paste_clipboards_manager_return_expiry (self);

    for (GSList *l = self->clipboards; l; l = l->next)
    {
        _Clipboard *clip = l->data;

        g_clear_slist (&clip->pending_refreshes, g_object_unref);
        g_clear_object (&clip->signal_group);
    }

    g_clear_object (&self->history_signals);
    g_clear_object (&self->history);
    g_clear_object (&self->settings);

    G_OBJECT_CLASS (g_paste_clipboards_manager_parent_class)->dispose (object);
}

static void
g_paste_clipboards_manager_finalize (GObject *object)
{
    GPasteClipboardsManager *self = G_PASTE_CLIPBOARDS_MANAGER (object);

    /* Not in dispose: an update in flight holds a ref on us precisely so that
     * the record its reply reads is still there, and a g_object_run_dispose ()
     * is dispose running while such a ref is out. */
    g_clear_slist (&self->clipboards, _clipboard_free);

    G_OBJECT_CLASS (g_paste_clipboards_manager_parent_class)->finalize (object);
}

static void
g_paste_clipboards_manager_class_init (GPasteClipboardsManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_clipboards_manager_dispose;
    object_class->finalize = g_paste_clipboards_manager_finalize;
}

static void
g_paste_clipboards_manager_init (GPasteClipboardsManager *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_clipboards_manager_new:
 * @history: (transfer none): a #GPasteHistory instance
 * @settings: (transfer none): a #GPasteSettings instance
 *
 * Create a new instance of #GPasteClipboardsManager
 *
 * Returns: a newly allocated #GPasteClipboardsManager
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClipboardsManager *
g_paste_clipboards_manager_new (GPasteHistory  *history,
                                GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (history), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    GPasteClipboardsManager *self = g_object_new (G_PASTE_TYPE_CLIPBOARDS_MANAGER, NULL);

    self->history = g_object_ref (history);
    self->settings = g_object_ref (settings);

    GSignalGroup *history_signals = self->history_signals = g_signal_group_new (G_PASTE_TYPE_HISTORY);
    g_signal_group_connect_swapped (history_signals, "selected", G_CALLBACK (on_item_selected), self);
    g_signal_group_set_target (history_signals, history);

    return self;
}
