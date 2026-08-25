// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-clipboards-manager.h>
#include <gpaste-daemon/gpaste-password-item.h>

typedef struct
{
    GPasteClipboardsManager *manager;
    GPasteClipboardProvider *clipboard;
    GSignalGroup            *signal_group;

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
    guint                    password_timeout_id;
    guint                    password_timeout;
    GPasteItem              *password;
} _Clipboard;

struct _GPasteClipboardsManager
{
    GObject parent_instance;

    GSList         *clipboards;
    GPasteHistory  *history;
    GSignalGroup   *history_signals;
    GPasteSettings *settings;
};

G_PASTE_DEFINE_TYPE (ClipboardsManager, clipboards_manager, G_TYPE_OBJECT)

static void g_paste_clipboards_manager_notify (GPasteClipboardProvider *clipboard, gpointer user_data);
static void g_paste_clipboards_manager_on_password_timeout (gpointer user_data);
static void g_paste_clipboards_manager_ensure_not_empty (_Clipboard *clip);

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

    return data;
}

static void
g_paste_clipboards_manager_bootstrap_ready (GPasteClipboardProvider *clipboard G_GNUC_UNUSED,
                                            GPasteItem              *item,
                                            gpointer                 user_data)
{
    g_autoptr (GPasteClipboardsManagerUpdateData) data = user_data;
    /* The update callback owns the item it is handed (transfer full); at
     * bootstrap we only care about the selection not being empty, so whatever
     * was already in it is read and dropped rather than pushed to the history. */
    g_autoptr (GPasteItem) bootstrapped = item;

    /* Disposed while the read was in flight: the history a selection would be
     * filled from is already gone. */
    if (!data->manager->history)
        return;

    g_paste_clipboards_manager_ensure_not_empty (data->clip);
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
                                          gboolean     something_in_clipboard)
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

            if (other == clip)
                continue;

            const gchar *text = g_paste_clipboard_provider_get_text (other->clipboard);

            if (!text || !g_paste_str_equal (text, synchronized_text))
                g_paste_clipboard_provider_select_text (other->clipboard, synchronized_text);
        }
    }
}

static void
g_paste_clipboards_manager_update_ready (GPasteClipboardProvider *clipboard,
                                         GPasteItem              *item,
                                         gpointer                 user_data)
{
    g_autoptr (GPasteClipboardsManagerUpdateData) data = user_data;
    _Clipboard *clip = data->clip;

    g_debug ("clipboards-manager: update ready");

    /* Disposed while the read was in flight: there is no history left to add to
     * and no settings left to ask, and the item we were handed is ours to
     * release. */
    if (!data->manager->history)
    {
        g_clear_object (&item);
        return;
    }

    const gchar *synchronized_text = NULL;

    if (item && g_paste_clipboard_provider_get_text (clipboard) &&
        g_paste_settings_get_synchronize_clipboards (clip->manager->settings))
        synchronized_text = g_paste_clipboard_provider_get_text (clipboard);

    if (!data->track && item)
        g_clear_object (&item);

    gboolean something_in_clipboard = !!g_paste_clipboard_provider_get_text (clipboard) ||
                                      !!g_paste_clipboard_provider_get_image_checksum (clipboard);

    g_paste_clipboards_manager_notify_finish (clip, item, synchronized_text, something_in_clipboard);
}

static void
g_paste_clipboards_manager_notify (GPasteClipboardProvider *clipboard,
                                   gpointer                 user_data)
{
    _Clipboard *clip = user_data;

    g_debug ("clipboards-manager: notify");

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

/* Whether every selection still carries @item, which is what says a password
 * expiring is the history's own selection to change rather than one provider's. */
static gboolean
g_paste_clipboards_manager_every_selection_holds (GPasteClipboardsManager *self,
                                                  GPasteItem              *item)
{
    for (GSList *clipboard = self->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        _Clipboard *clip = clipboard->data;

        if (!g_paste_clipboards_manager_selection_holds (clip->clipboard, item))
            return FALSE;
    }

    return TRUE;
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
g_paste_clipboards_manager_arm_password (_Clipboard *clip,
                                         GPasteItem *item)
{
    guint timeout = (G_PASTE_IS_PASSWORD_ITEM (item)) ? g_paste_password_item_get_timeout (G_PASTE_PASSWORD_ITEM (item)) : 0;

    /* The same password reaching this selection again is the exposure already
     * running out, not a new one: a selection re-taken every few seconds would
     * otherwise push its countdown back for as long as that kept happening.
     * Unless the duration itself moved, which is the one thing about an item
     * already armed for that MakePassword can change under it. */
    if (item == clip->password && timeout == clip->password_timeout)
        return;

    /* Whatever this selection's countdown was armed for is what @item has just
     * replaced on it, and no other selection's is any of this one's business. */
    g_clear_handle_id (&clip->password_timeout_id, g_source_remove);
    g_clear_object (&clip->password);
    clip->password_timeout = 0;

    if (!timeout)
        return;

    clip->password = g_object_ref (item);
    clip->password_timeout = timeout;
    clip->password_timeout_id = g_timeout_add_seconds_once (timeout, g_paste_clipboards_manager_on_password_timeout, clip);
    g_source_set_name_by_id (clip->password_timeout_id, "[GPaste] password timeout");
}

/* A provider holding nothing re-owns its selection with the history's head,
 * which puts an item on it without going through select (): the countdown that
 * item may need is armed here instead. */
static void
g_paste_clipboards_manager_ensure_not_empty (_Clipboard *clip)
{
    GPasteItem *item = g_paste_clipboard_provider_ensure_not_empty (clip->clipboard, clip->manager->history);

    if (item)
        g_paste_clipboards_manager_arm_password (clip, item);
}

/* Take the password @clip's countdown was armed for back off its selection, if
 * that selection still carries it. One that has moved on holds what replaced it,
 * which is the user's and not ours to overwrite -- and that is also what makes a
 * stale firing harmless, the item deleted or the history switched, so none of
 * those needs a cancellation of its own. */
static void
g_paste_clipboards_manager_deselect_password (_Clipboard *clip)
{
    GPasteClipboardsManager *self = clip->manager;

    g_clear_handle_id (&clip->password_timeout_id, g_source_remove);
    clip->password_timeout = 0;

    g_autoptr (GPasteItem) password = g_steal_pointer (&clip->password);

    /* Dispose runs this before it releases the history, and can run twice. */
    if (!password || !self->history)
        return;

    if (!g_paste_clipboards_manager_selection_holds (clip->clipboard, password))
        return;

    g_debug ("clipboards-manager: deselecting the password");

    /* What goes in its place: the first item that is not a password. */
    const GPtrArray *history = g_paste_history_get_history (self->history);
    GPasteItem *replacement = NULL;

    for (guint i = 0; !replacement && i < history->len; ++i)
    {
        GPasteItem *item = g_ptr_array_index (history, i);

        if (!G_PASTE_IS_PASSWORD_ITEM (item))
            replacement = item;
    }

    /* Nothing has moved on, so the history's own selection is what has to
     * change: selecting moves the replacement to the front, taking the password
     * out of the active slot -- where ensure_not_empty () would otherwise put it
     * straight back on the next selection to fall empty -- and drives every
     * provider through the ordinary path, which is what takes the password off
     * the other selections and retires the countdowns they armed for it. */
    if (replacement && g_paste_clipboards_manager_every_selection_holds (self, password))
    {
        g_paste_history_select (self->history, g_paste_item_get_uuid (replacement));
        return;
    }

    /* With no item to put there the empty string goes on instead, through
     * select_text () rather than by dropping the content: a provider reports
     * emptiness by the *kind* it holds, so an empty string still counts as
     * something and ensure_not_empty () leaves it alone, where a genuinely empty
     * selection would have it re-select the history's head -- the very password
     * just taken off. */
    if (!replacement || !g_paste_clipboard_provider_select_item (clip->clipboard, replacement))
        g_paste_clipboard_provider_select_text (clip->clipboard, "");
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
 * Take a pending password off the clipboard now rather than wait for its timeout
 *
 * Meant for the paths where there is no later to wait for: a daemon standing down
 * leaves the selections behind it, so a password still on one would stay there
 * for the rest of the session with nothing left to take it back off. Does
 * nothing for a selection with no countdown running.
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_expire_password (GPasteClipboardsManager *self)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));

    for (GSList *clipboard = self->clipboards; clipboard; clipboard = g_slist_next (clipboard))
        g_paste_clipboards_manager_deselect_password (clipboard->data);
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

        if (!g_paste_clipboards_manager_selection_holds (clip->clipboard, item))
            continue;

        g_paste_clipboards_manager_arm_password (clip, item);
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
        g_paste_clipboards_manager_arm_password (clip, item);
    }

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
