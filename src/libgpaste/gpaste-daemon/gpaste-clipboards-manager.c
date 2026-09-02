// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-error.h>
#include <gpaste-3/gpaste-update-enums.h>
#include <gpaste-daemon/gpaste-clipboards-manager.h>
#include <gpaste-daemon/gpaste-clipboard-provider-private.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-text-item.h>

/* Here and not in gpaste-macros.h beside g_paste_weak_ref_free (), which is an
 * installed header: an autoptr cleanup defined there is defined for every
 * libgpaste consumer, and one that defines its own for GWeakRef -- or a GLib that
 * ships one -- would then fail to build on a redefinition. This file is the one
 * that needs it. */
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GWeakRef, g_paste_weak_ref_free)

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
    /* A strip this selection is owed and could not be given: see
     * finish_refresh (). Both slots outlive a change -- the read that could not
     * say what the selection holds cannot say a change happened either -- and
     * what tells them apart is that a read has already looked at this one: it is
     * retried ahead of @pending_refreshes, which holds copies awaiting
     * identification. Its value is what says whether it is still the one to
     * publish, and anything this daemon writes to the selection retires it.
     *
     * Weak like @pending_refreshes' own, and for the same reason: an entry the
     * user deletes while its strip is owed has nothing left to publish, and a
     * slot that may be kept until the next read of a selection nobody touches
     * again is one that would hold that entry's cleartext for the session.
     * finish_refresh () already checks the history before publishing, so nothing
     * here needs the item to survive it. */
    GWeakRef                *refused_refresh;
    guint64                  change_serial;
    /* Bumped by every write this daemon makes to the selection, and by every
     * change it hears about -- published () and notify (), and nowhere else:
     * what a sync in flight compares against to know that the pair it was
     * requested for is still the pair it would land on, and what
     * @synced_generation is compared against, which relies on "nowhere else" as
     * much as on "every" (see there). */
    guint64                  generation;
    gboolean                 sync_requested;
    /* A cooldown coalesces strip bursts while allowing a later request to retry
     * an unchanged owner. A new generation can be identified immediately. */
    gint64                   refresh_read_after;

    /* The password this selection is carrying -- ref'd, since it may leave the
     * history while still sitting there, and its value is what says whether the
     * selection still carries it -- and the countdown taking it back off.
     *
     * Recorded whatever the countdown: a password-timeout of 0 leaves the
     * password sitting there with nothing to take it off, which is a fact about
     * the selection and not the absence of one -- and what the selection carries
     * is the question every route but the countdown's own is asking.
     * @password_timeout_id is therefore what says there is a countdown, not
     * @password.
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
    /* The last read of this selection could not say whether @password is still
     * on it: it identified no content (a text read that failed, or that the
     * guard gave up on) and the hint said nothing either way, or said only that
     * some secret is there (confirm_password ()). The cache
     * then holds no value, which selection_holds () reads as "moved on" -- but
     * nothing moved, the read just saw nothing, and dropping the record on that
     * would leave the cleartext there with no record to refuse a bare-text sync
     * with. So while this is set the record stands (forget_stale_password ()),
     * a sync out of this selection is refused (sync_from_to ()), and a countdown
     * that runs out marks the password overdue rather than taking it off
     * (deselect_password ()): nothing is published over a selection nobody has
     * identified, and the read that does identify it acts on the deadline --
     * one the countdown asks for itself, there being no other read owed.
     *
     * Recomputed by every read that concludes with an answer to give, and
     * cleared by whatever this daemon puts on the selection itself. */
    gboolean                 password_unconfirmed;
    /* The @generation a synchronization left this selection at when it published
     * @password here from the other selection -- notify_finish () or
     * sync_from_to () -- or 0: see is_synced_copy () and retire_synced_copies ().
     *
     * Nothing clears it: @generation moves on every write this daemon makes to
     * the selection and every change it hears of, and on nothing else, so the
     * record is still the synchronization's copy exactly as long as the two
     * agree, whichever route moved it. The user selecting that very item, an
     * owner putting it back, anything else written over it, all move it; a
     * re-arm -- a timeout edit, a resolved pending edit, a link to the loaded
     * history, the record moving onto another object of the same value -- or an
     * identifying read, reading the copy back, do not, none of them changing who
     * put the password there. Which is why @generation must move nowhere else:
     * a bump where no owner changed -- a read starting, an identifying read, a
     * re-arm -- would stop a synchronized copy from ever being taken off. */
    guint64                  synced_generation;
    /* The record is a capture identify_ready () found no history head to link
     * to: see link_captures (). */
    gboolean                 link_on_load;
    /* When the record's countdown started, which the arming that created the
     * record says and nothing after it moves: an identical re-read keeps the
     * countdown running (arm_password_at ()), and so keeps this. */
    gint64                   armed_from;
    /* Edits name items, not selections. Keep each candidate until the read
     * identifies its text; deadlines start at the edit, not at the reply. */
    GSList                  *pending_passwords;
    gboolean                 force_expiry;

    /* The selection said it carries a secret and the read produced no item to
     * carry it as -- a value the size policy turned down, a text read that
     * failed beside a hint that came back. There is nothing to record and
     * nothing to take back off, and the exposure is the same one @password is
     * here for: what sits there is a cleartext, and syncing it publishes that
     * cleartext stripped of the hint that keeps other clipboard managers from
     * recording it. So the answer is to leave the other selection alone.
     *
     * Set by a read of this selection, which is the only thing that says what an
     * app put there, and cleared by whatever the daemon puts there itself
     * (publish_item (), publish_text ()), that being content it does hold. A
     * record left standing over one of those is one no read is owed for: on a
     * selection this daemon wrote to last, nothing else will clear it for the
     * rest of the session, and sync_from_to () goes on refusing a selection
     * carrying ordinary text. Which is why every route that writes to a
     * selection goes through one of those calls, the sync's own completion and
     * the re-own a trimmed text asks for included -- the latter reported back as
     * the answer it makes true rather than published from here, the write being
     * the read's own (see g_paste_clipboard_update_conclude ()).
     *
     * Never cleared on the strength of what a selection is believed to carry: the
     * only such belief available is the text cache, which holds the last read
     * that landed, and a record standing is precisely a read that did not.
     *
     * Retired only by a read that came back with an answer: unlike @password
     * there is no item here to ask selection_holds () about -- this is the case
     * where the read produced none -- so what a read says is the only thing that
     * can retire it, and a read that could not tell (%CLIPBOARD_SECRET_UNKNOWN)
     * says nothing. Dropping it on that would drop it over the secret still
     * sitting there, which is the whole of what it is for. */
    gboolean                 sensitive;
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
    /* The idle linking startup captures to the history: see link_captures (). */
    guint           link_source_id;
};

G_PASTE_DEFINE_TYPE (ClipboardsManager, clipboards_manager, G_TYPE_OBJECT)

static void g_paste_clipboards_manager_notify (GPasteClipboardProvider *clipboard, gpointer user_data);
static void g_paste_clipboards_manager_deselect_password (_Clipboard *clip);
static void g_paste_clipboards_manager_publish_over_password (_Clipboard *clip);
static void g_paste_clipboards_manager_retire_synced_copies (_Clipboard *clip);
static void g_paste_clipboards_manager_on_password_timeout (gpointer user_data);
static void g_paste_clipboards_manager_ensure_not_empty (_Clipboard *clip);
static void g_paste_clipboards_manager_arm_password (_Clipboard *clip, GPasteItem *item, gboolean renew_expired);
static gboolean g_paste_clipboards_manager_selection_holds (GPasteClipboardProvider *clipboard, GPasteItem *item);
static void g_paste_clipboards_manager_forget_stale_password (_Clipboard *clip);
static gboolean g_paste_clipboards_manager_publish_item (_Clipboard *clip, GPasteItem *item, gboolean independent);
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

/* Whether a selection's record outlived the read that was to confirm it: the
 * password it names may be sitting there still, and nothing is coming to say
 * (see @password_unconfirmed on _Clipboard). Unlike expiry_pending (), this is
 * not something to wait for -- the read it would wait for has already
 * answered -- so it is what the wait reports rather than what holds it open.
 *
 * Which is why a waiter asks for a read of its own first
 * (reread_unconfirmed ()): what is left here is a selection that was asked again
 * and still said nothing, and that is a failure a caller is owed rather than one
 * more wait. */
static gboolean
g_paste_clipboards_manager_expiry_unresolved (GPasteClipboardsManager *self)
{
    for (GSList *l = self->clipboards; l; l = l->next)
    {
        _Clipboard *clip = l->data;

        if (clip->password_unconfirmed &&
            (clip->password_timeout || clip->password_expired || clip->pending_passwords))
            return TRUE;
    }

    return FALSE;
}

typedef struct
{
    guint    timeout_id;
    gboolean timed_out;
    gboolean unresolved;
} ExpiryWait;

static gboolean
return_expiry_on_idle (gpointer user_data)
{
    GTask *task = user_data;
    ExpiryWait *wait = g_task_get_task_data (task);

    if (wait->timed_out)
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "Password expiry could not identify the selections before its deadline.");
    else if (wait->unresolved)
    {
        /* G_PASTE_ERROR: the daemon hands this to a Reexecute caller as it is,
         * and a G_IO_ERROR there reads as the transport failing. */
        g_task_return_new_error (task, G_PASTE_ERROR, G_PASTE_ERROR_FAILED,
                                 "A password selection could not be identified for expiry.");
    }
    else
        g_task_return_boolean (task, TRUE);
    return G_SOURCE_REMOVE;
}

static void
return_expiry_task (GTask    *task,
                    gboolean  timed_out,
                    gboolean  unresolved)
{
    ExpiryWait *wait = g_task_get_task_data (task);

    g_clear_handle_id (&wait->timeout_id, g_source_remove);
    wait->timed_out = timed_out;
    wait->unresolved = unresolved;
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
    gboolean unresolved = g_paste_clipboards_manager_expiry_unresolved (self);
    g_autoslist (GTask) tasks = g_steal_pointer (&self->expiry_tasks);

    for (GSList *l = tasks; l; l = l->next)
        return_expiry_task (l->data, FALSE, unresolved);
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
        return_expiry_task (task, TRUE, FALSE);
        break;
    }

    /* A later request keeps its own deadline and classification cleanup. */
    g_paste_clipboards_manager_release_forced_expiry (self);

    return G_SOURCE_REMOVE;
}

/* What a read in flight has to keep alive -- an update's, or a sync's. A provider
 * refs itself for the whole read; nothing was holding this side up. The manager
 * owns the _Clipboard records and everything the reply then reaches for, so a ref
 * on it is what makes @clip still be there when that reply lands. @track says
 * whether the item an update brings back is one to keep, and means nothing to a
 * sync, which brings back none. */
typedef struct
{
    GPasteClipboardsManager *manager;
    _Clipboard              *clip;
    gboolean                 track;
    guint64                  change_serial;
    _Clipboard              *source;
    guint64                  source_generation;
    guint64                  destination_generation;
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

/* What a write the manager itself makes leaves behind: content it holds, which is
 * the one thing @sensitive on _Clipboard says the selection does not carry. What
 * every write leaves behind, whoever made it, is noted where it is announced
 * instead (published ()).
 *
 * The strips owed to this selection go here too, this being where a write is
 * known to have landed: what they would put there is precisely what was just
 * put there instead, and @refused_refresh is retired by anything this daemon
 * writes (see its comment on _Clipboard). Asking for a write is not making one,
 * so a refusal must leave them standing -- which is why they are dropped here
 * and not by publish_item () before it knows. */
static void
g_paste_clipboards_manager_note_published (_Clipboard *clip)
{
    clip->refresh_read_after = 0;
    clip->sensitive = FALSE;
    clip->password_unconfirmed = FALSE;
    g_clear_slist (&clip->pending_refreshes, g_paste_weak_ref_free);
    g_clear_pointer (&clip->refused_refresh, g_paste_weak_ref_free);
}

/* Put @item on @clip's selection, answering whether it got there.
 *
 * Every write the manager makes to a selection goes through this or through
 * publish_text (). A refusal put nothing there, so every record it holds still
 * stands -- the password it names, the secret it is carrying, and the strips it
 * is owed alike (note_published ()).
 *
 * @independent says whether this write is the user choosing what the selection
 * carries -- select () and clear (), which end the copy that was on it -- or
 * maintenance on the copy already there: a trimmed text re-owned, a strip
 * published, a sync carried over, a password taken off. Only the first advances
 * the copy serial the synchronization in notify_finish () orders itself by;
 * maintenance preserves it, so a write made on behalf of an older copy cannot
 * overwrite a newer one. */
static gboolean
g_paste_clipboards_manager_publish_item (_Clipboard *clip,
                                         GPasteItem *item,
                                         gboolean    independent)
{
    if (!g_paste_clipboard_provider_select_item_full (clip->clipboard, item, independent))
        return FALSE;

    g_paste_clipboards_manager_note_published (clip);

    return TRUE;
}

/* Put @text on @clip's selection. As in publish_item (): what it carries
 * afterwards is ours, and @independent says the same thing. */
static void
g_paste_clipboards_manager_publish_text (_Clipboard  *clip,
                                         const gchar *text,
                                         gboolean     independent)
{
    g_paste_clipboard_provider_select_text_full (clip->clipboard, text, independent);
    g_paste_clipboards_manager_note_published (clip);
}

/* Requests borrow their snapshots: only items still in the history can be
 * published. Pruning and coalescing bound the queue by those live snapshots. */
static void
g_paste_clipboards_manager_queue_refresh (_Clipboard *clip,
                                          GPasteItem *item)
{
    GSList **link = &clip->pending_refreshes;
    gboolean found = FALSE;

    while (*link)
    {
        GSList *node = *link;
        GWeakRef *pending = node->data;
        g_autoptr (GPasteItem) candidate = g_weak_ref_get (pending);

        if (!candidate || g_paste_history_get_by_uuid (clip->manager->history, g_paste_item_get_uuid (candidate)) != candidate)
        {
            *link = node->next;
            g_paste_weak_ref_free (pending);
            g_slist_free_1 (node);
            continue;
        }
        found |= candidate == item;
        link = &node->next;
    }

    if (!found)
        clip->pending_refreshes = g_slist_prepend (clip->pending_refreshes, g_paste_weak_ref_new (item));
}

/* Keep @item as the strip @clip is owed and could not be given. One slot: a
 * second refusal is a later read's answer about the same selection, so it
 * replaces the first. Held weakly, as @refused_refresh says. */
static void
g_paste_clipboards_manager_refuse_refresh (_Clipboard *clip,
                                           GPasteItem *item)
{
    g_clear_pointer (&clip->refused_refresh, g_paste_weak_ref_free);
    clip->refused_refresh = g_paste_weak_ref_new (item);
}

/* A strip can arrive before the selection's text is known. Only publish the
 * requested snapshot while it is still the history's item, and substitute it
 * for the read's item so that stale formats cannot enter the history again.
 *
 * Published only over a selection this read says holds no secret: @secret
 * stands in for refresh_text ()'s @sensitive and @password_unconfirmed guards,
 * which notify_finish () has yet to write from it. Anything but a no may be a
 * secret, and the plain text carries no hint
 * (/clipboard/strip_during_read/unknown_hint). A record of the same value is no
 * reason to refuse here, the read having just answered for the selection
 * (/clipboard/selection_identity/strip/duplicate).
 *
 * A refusal is not the request being spent. The history was stripped by the
 * method before this read concluded, and the read's item is substituted whatever
 * happens below -- letting the rich one through would put the very formats the
 * user asked to drop straight back into the history -- so dropping the request
 * here would leave the history stripped and the selection still serving the rich
 * flavours, with nothing ever coming to correct them. It is kept in
 * @refused_refresh instead, for the next read that does identify this selection
 * (/clipboard/strip_during_read/retried_after_unknown_hint), which is also where
 * it is retried from first. A selection that really did move on drops it here on
 * its own: selection_holds () then answers no and nothing is put back.
 *
 * This call is the only thing that spends a request: a change leaves both slots
 * alone (notify ()), so a read overtaken by another leaves what it was owed to
 * the one overtaking it. */
static void
g_paste_clipboards_manager_finish_refresh (_Clipboard           *clip,
                                           GPasteItem          **item,
                                           GPasteClipboardSecret secret)
{
    /* Silence leaves both retry slots and their ordering intact. */
    if (secret == CLIPBOARD_SECRET_UNKNOWN && !g_paste_clipboard_provider_get_text (clip->clipboard))
        return;

    g_autoslist (GWeakRef) pending = g_steal_pointer (&clip->pending_refreshes);
    g_autoptr (GWeakRef) refused = g_steal_pointer (&clip->refused_refresh);

    /* The read found a password with the stripped text's value: that is what the
     * selection holds, and the plain text would publish it without its hint.
     *
     * The requests go with it rather than being kept in @refused_refresh: that
     * slot is for a read that could not say what the selection holds, and this
     * one said. A selection carrying a password is refused a strip for as long
     * as it carries one -- refresh_text () asks the same of every selection it
     * reaches, and refuses it there outright -- so a request kept here would be
     * one no later read could ever act on either. */
    if (*item && G_PASTE_IS_PASSWORD_ITEM (*item))
        return;

    /* Retried ahead of this read's own requests, and under the same guards: a
     * strip kept from an earlier read is the older one. The slot's reference
     * moves into the list rather than being copied, the list being what frees
     * every request this read spends. */
    if (refused)
        pending = g_slist_prepend (pending, g_steal_pointer (&refused));

    for (GSList *l = pending; l; l = l->next)
    {
        GWeakRef *refresh = l->data;
        g_autoptr (GPasteItem) plain = g_weak_ref_get (refresh);

        if (!plain || g_paste_history_get_by_uuid (clip->manager->history, g_paste_item_get_uuid (plain)) != plain)
            continue;

        if (g_paste_clipboards_manager_selection_holds (clip->clipboard, plain))
        {
            if (secret != CLIPBOARD_SECRET_NO || !g_paste_clipboards_manager_publish_item (clip, plain, FALSE))
                g_paste_clipboards_manager_refuse_refresh (clip, plain);

            if (*item)
                g_set_object (item, plain);
            break;
        }
    }
}

/* Resolve timeout edits before expiry: an edit may cancel or shorten the
 * deadline that elapsed during classification. Neither may synchronize an
 * overdue password back onto another selection.
 *
 * An edit is resolved on the value its item carries and on nothing else, which
 * is the whole of what rearm_password () asks when no read is in flight: a
 * MakePassword must not come out two ways depending on whether a read happened
 * to be out when the user made it. What the owner said about a secret is not
 * that question -- ordinary text with no hint on it is what every MakePassword
 * is made of -- and the price of the value alone is the one notify_finish ()
 * already names: the user copying that very string as ordinary text keeps the
 * countdown, which then takes their own copy back off.
 *
 * @item and @secret are the read's, when a read is what resolves this; every
 * other caller passes %NULL and %CLIPBOARD_SECRET_UNKNOWN. */
static gboolean
g_paste_clipboards_manager_finish_expiry (_Clipboard           *clip,
                                          GPasteItem          **item,
                                          GPasteClipboardSecret secret)
{
    if (g_paste_clipboard_provider_is_reading (clip->clipboard))
        return FALSE;

    /* No value is not evidence against an edit when classification failed.
     * Keep its deadline until a read or publication identifies the selection. */
    g_autoslist (PendingPassword) pending = clip->password_unconfirmed ? NULL : g_steal_pointer (&clip->pending_passwords);

    for (GSList *l = pending; l; l = l->next)
    {
        PendingPassword *change = l->data;

        if (g_paste_clipboards_manager_selection_holds (clip->clipboard, change->item))
        {
            g_paste_clipboards_manager_arm_password_at (clip, change->item, change->timeout, change->deadline, change->changed);
            /* An owner saying its text is no secret makes no password of it: the
             * read keeps its own item, as preserve_password () does for a record
             * that value keeps standing. The countdown is armed all the same --
             * see the note on the value alone above
             * (/clipboard/timeout_edit/plain_selection).
             *
             * The captured twin carries the settings default, not this edit.
             * Use the edited item throughout arming, history and sync below. */
            if (item && *item && secret != CLIPBOARD_SECRET_NO)
                g_set_object (item, change->item);
            break;
        }
    }

    /* Forcing brings a running countdown forward, and what resolved since the
     * request -- an edit, a publication -- may have left none to bring. */
    if (clip->force_expiry && !clip->password_timeout)
    {
        clip->force_expiry = FALSE;
        g_paste_clipboards_manager_complete_expiry (clip->manager);
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
        g_paste_clipboards_manager_finish_expiry (l->data, NULL, CLIPBOARD_SECRET_UNKNOWN);
}

static void
g_paste_clipboards_manager_resume_sync (_Clipboard *clip)
{
    if (!clip->sync_requested || !clip->manager->history ||
        g_paste_clipboard_provider_is_reading (clip->clipboard))
        return;

    clip->sync_requested = FALSE;
    g_paste_clipboards_manager_sync_from_to (clip->manager, g_paste_clipboard_provider_is_clipboard (clip->clipboard));
}

/* Whether the read that just concluded left @clip's standing record unconfirmed:
 * see @password_unconfirmed on _Clipboard. Asked before anything else acts on the
 * read -- finish_expiry () included, which would otherwise take the previous
 * read's word for a selection this one has just identified. A superseded read
 * says nothing about the selection and leaves the flag to the read overtaking
 * it.
 *
 * Any answer but a no counts, %CLIPBOARD_SECRET_YES included: a hint that came
 * back with no text beside it -- the text read failed, or the guard concluded
 * it -- says the selection carries *a* secret, not which one, so it confirms
 * the record no more than silence does. Taking it for a move would retire the
 * record and its countdown, and with no record nothing asks for the re-read
 * that would find the password still sitting there
 * (/clipboard/unconfirmed_password/secret_without_text/expiry). Only a no says
 * the selection holds no password at all. */
static void
g_paste_clipboards_manager_confirm_password (_Clipboard           *clip,
                                             GPasteClipboardSecret secret)
{
    clip->password_unconfirmed = (clip->password || clip->pending_passwords) && secret != CLIPBOARD_SECRET_NO &&
                                 !g_paste_clipboard_provider_get_text (clip->clipboard) &&
                                 !g_paste_clipboard_provider_get_image_checksum (clip->clipboard);
}

/* Whether @clip's selection may be carrying a secret, which is what every route
 * about to write plain text over it -- or to read plain text out of it -- has to
 * ask: a password it is recorded as holding, a secret no item was made of, or a
 * record its last read could not confirm. The three are one question, and asked
 * once so that a fourth of them, or a fourth #GPasteClipboardSecret state, is
 * added in one place rather than remembered in each.
 *
 * A read that has just concluded answers it itself, and the fields here are what
 * notify_finish () is about to write from that answer: finish_refresh () therefore
 * asks %CLIPBOARD_SECRET_NO of the read rather than asking this of the fields it
 * has yet to overwrite. Same question, newer evidence. */
static gboolean
g_paste_clipboards_manager_may_carry_secret (_Clipboard *clip)
{
    return clip->password || clip->sensitive || clip->password_unconfirmed;
}

static void
g_paste_clipboards_manager_preserve_password (_Clipboard           *clip,
                                              GPasteItem          **item,
                                              GPasteClipboardSecret secret)
{
    /* A matching exposure keeps its item and deadline even when its hint is
     * readable: the captured twin only knows the global timeout default. */
    if (*item && secret != CLIPBOARD_SECRET_NO && clip->password &&
        g_paste_clipboards_manager_selection_holds (clip->clipboard, clip->password))
        g_set_object (item, clip->password);
}

/* A read asked so that the selection is identified, not so that a copy is
 * recorded: the one that bootstraps a provider, and the one a waiter for expiry
 * asks for (reread_unconfirmed ()).
 *
 * It goes nowhere near notify_finish (), which is the whole point: nothing here
 * enters the history and nothing is synchronized onto the other selection. A
 * sync follows the copy the user made, and neither of these is one -- the
 * selection is carrying what it was carrying before the question was asked. */
static void
g_paste_clipboards_manager_identify_ready (GPasteClipboardProvider *clipboard G_GNUC_UNUSED,
                                           GPasteItem              *item,
                                           gboolean                 superseded,
                                           GPasteClipboardSecret    secret,
                                           gpointer                 user_data)
{
    g_autoptr (GPasteClipboardsManagerUpdateData) data = user_data;
    /* The update callback owns the item it is handed (transfer full); only what
     * the selection carries is at stake here, so whatever was already in it is
     * read and dropped rather than pushed to the history. */
    g_autoptr (GPasteItem) identified = item;

    if (!superseded)
    {
        g_paste_clipboards_manager_confirm_password (data->clip, secret);
        g_paste_clipboards_manager_preserve_password (data->clip, &identified, secret);
    }

    /* A superseded read can finish expiry only when a publication, rather than
     * another read, replaced it. It must never restore the history itself. */
    if (g_paste_clipboards_manager_finish_expiry (data->clip, &identified, secret) || superseded || !data->manager->history)
    {
        g_paste_clipboards_manager_resume_sync (data->clip);
        return;
    }

    /* A password already sitting on the selection -- when the daemon started or
     * re-executed, or when the read that was to confirm it said nothing -- is on
     * it just the same: nothing else would ever record it, and every route that
     * asks what this selection carries -- sync_from_to () above all -- would then
     * answer with the cleartext, stripped of what says it is a secret. */
    if (G_PASTE_IS_PASSWORD_ITEM (identified))
    {
        gboolean headless = FALSE;

        if (!data->clip->password)
        {
            /* A head seen while the history is still loading is one the load is
             * about to throw away -- an add made in the meantime -- and not the
             * one to link against, as link_captures () says for itself
             * (/clipboard/bootstrap_before_load/identify_during_load). */
            gboolean loading = g_paste_history_is_loading (data->manager->history);
            g_autoptr (GPasteItem) head = (loading) ? NULL : g_paste_history_dup (data->manager->history, 0);

            /* An initial capture equal to the history head shares its identity
             * and stored timeout. Later reads preserve the standing record. */
            if (head && g_paste_item_equals (identified, head))
                g_set_object (&identified, head);
            headless = !head;
        }
        g_paste_clipboards_manager_arm_password (data->clip, identified, FALSE);
        /* No head to link to may be a history still loading, the daemon
         * starting its bootstrap reads before its history load: the link is
         * then made once it has loaded (link_captures ()). */
        if (headless && data->clip->password == identified)
            data->clip->link_on_load = TRUE;
    }
    /* A record already standing is retired only once the selection has moved
     * on from what it names, as in notify_finish (): the question is what the
     * cache holds now, not whether this read produced an item -- an owner
     * re-asserting the same text with no hint on it produces none and has moved
     * nothing (/clipboard/unconfirmed_password/reread_keeps_record). */
    else
        g_paste_clipboards_manager_forget_stale_password (data->clip);

    /* And a secret the read could make no item of is on the selection just the
     * same: see @sensitive on _Clipboard. Proof is what makes one here, an
     * unanswered hint included: there is no record yet for an unknown to keep,
     * and inventing one would refuse every sync out of this selection for the
     * rest of the session on no evidence at all.
     *
     * Retired under notify_finish ()'s guard and read the same way, this being
     * the same record and the same question: %CLIPBOARD_SECRET_UNKNOWN is a read
     * that said nothing, and a re-read asked for *because* the selection could
     * not be identified is the last thing that may conclude it moved on -- it
     * would drop the record over the very cleartext it is kept for
     * (/clipboard/unrecorded_secret/reread_keeps_record). A password record
     * standing beside it is what that secret is, which makes it a recorded one
     * and so not this. */
    if (secret != CLIPBOARD_SECRET_UNKNOWN)
        data->clip->sensitive = (secret == CLIPBOARD_SECRET_YES) && !data->clip->password;

    g_paste_clipboards_manager_finish_refresh (data->clip, &identified, secret);
    g_paste_clipboards_manager_ensure_not_empty (data->clip);
    /* As in notify_finish (): a password this read is the first to see gone may
     * have a synchronized copy left. */
    g_paste_clipboards_manager_retire_synced_copies (data->clip);
    g_paste_clipboards_manager_resume_sync (data->clip);
}

/* Ask for the read that identifies @clip's selection, answered by
 * identify_ready (), unless a read is already out -- that one is the answer on
 * its way. Every route asking what a selection carries, rather than recording a
 * copy of it, goes through here, so the way such a read is asked for is written
 * once. */
static void
g_paste_clipboards_manager_identify (_Clipboard *clip)
{
    if (g_paste_clipboard_provider_is_reading (clip->clipboard))
        return;

    g_paste_clipboard_provider_update (clip->clipboard, g_paste_clipboards_manager_identify_ready,
                                       g_paste_clipboards_manager_update_data_new (clip, FALSE));
}

/* Every write to this selection is announced here, the manager's own and the one
 * it does not make: the re-own a trimmed text asks for is the read's, made as it
 * concludes (g_paste_clipboard_update_conclude ()). A sync reply is one of the
 * manager's own: sync_ready () publishes it through publish_text ().
 *
 * Which is why the generation a sync in flight tells its own pair apart by is
 * taken here and not in note_published (), the one place a write the manager did
 * not make does not reach: a generation left standing across that re-own is one a
 * synchronization requested before the trim is then let through over, putting the
 * text as it was read back over the trimmed text that replaced it
 * (/clipboard/sync_reply_after_trimmed_reown).
 *
 * The strips are the other way round, and that is the whole of the division: a
 * write of the manager's own puts there precisely what they would have put there
 * (note_published ()), where the trimmed re-own is followed by the very
 * conclusion that spends them, a moment later
 * (/clipboard/strip_during_read/trimmed). So they are retired here only for the
 * writes note_published () never sees, which is what @independent narrows this
 * to: a strip applies to the copy being read when it was requested, not a later
 * copy of equal text, and a local publication ends that copy without a read
 * (/clipboard/stale_strip_after_publication).
 *
 * Announced from inside the write itself, so the generation moves before
 * note_published () settles the rest of what that write leaves behind. Nothing
 * reads either in between -- the emission is synchronous, and this touches only
 * @clip -- so that order is one to keep rather than one to lean on: anything
 * added here that reached back into the manager would find the selection half
 * settled. */
static void
g_paste_clipboards_manager_published (GPasteClipboardProvider *clipboard G_GNUC_UNUSED,
                                      gboolean                 independent,
                                      gpointer                 user_data)
{
    _Clipboard *clip = user_data;

    ++clip->generation;

    if (independent)
    {
        g_clear_slist (&clip->pending_refreshes, g_paste_weak_ref_free);
        g_clear_pointer (&clip->refused_refresh, g_paste_weak_ref_free);
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
    g_paste_clipboard_provider_update (clipboard, g_paste_clipboards_manager_identify_ready,
                                       g_paste_clipboards_manager_update_data_new (clip, FALSE));
}

/* A sync read has landed: the text goes on the destination the way everything
 * else the manager writes does, so that what that selection carries and what its
 * records say are settled together. The backend publishing it itself would leave
 * both saying what the selection carried before -- and nothing would come to
 * correct them, a selection the daemon owns raising no external change to read.
 */
static void
g_paste_clipboards_manager_sync_ready (GPasteClipboardProvider *other G_GNUC_UNUSED,
                                       const gchar             *text,
                                       gpointer                 user_data)
{
    GPasteClipboardsManagerUpdateData *data = user_data;

    /* A reply belongs to the pair of selections it was requested for. Source
     * changes can introduce a secret; destination changes supersede the write. */
    if (!data->manager->history || data->clip->generation != data->destination_generation)
        return;

    /* A source that moved still owes the destination the sync that was asked
     * for, of what it holds now: it is requested again, as sync_from_to () does
     * for a source being read (/clipboard/sync_source_changed). */
    if (data->source->generation != data->source_generation ||
        g_paste_clipboard_provider_is_reading (data->source->clipboard))
    {
        data->source->sync_requested = TRUE;
        g_paste_clipboards_manager_resume_sync (data->source);
        return;
    }

    /* Named for the copy it now carries, as the password branch of
     * sync_from_to () names it for the write it makes: the same invariant, the
     * other half of the same request. */
    data->clip->change_serial = MAX (data->clip->change_serial, data->source->change_serial);
    g_paste_clipboards_manager_publish_text (data->clip, text, FALSE);
    g_paste_clipboards_manager_forget_stale_password (data->clip);
    /* As in sync_from_to ()'s password branch: a write to this selection is what
     * an edit or a forced expiry waiting on it was waiting for, and this is the
     * one publishing route with nothing after it to resolve them. */
    g_paste_clipboards_manager_finish_expiry (data->clip, NULL, CLIPBOARD_SECRET_UNKNOWN);
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

    _Clipboard *_from = NULL;
    _Clipboard *_to = NULL;

    g_debug ("clipboards-manager: sync_from_to");

    for (GSList *clipboard = self->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        _Clipboard *_clip = clipboard->data;

        if (g_paste_clipboard_provider_is_clipboard (_clip->clipboard) == from_clipboard)
            _from = _clip;
        else
            _to = _clip;
    }

    if (!_from || !_to)
        return;

    /* Classification owns the answer about a secret. A pending cache cannot
     * authorize a bare-text read or retire the password it last identified. */
    if (g_paste_clipboard_provider_is_reading (_from->clipboard))
    {
        _from->sync_requested = TRUE;
        return;
    }

    /* A password the source selection is still carrying travels as the item it
     * is: syncing it as bare text would publish the cleartext with nothing
     * saying it is a secret, which is the whole of what keeps other clipboard
     * managers from recording it -- and would leave it on a selection with no
     * countdown to take it back off.
     *
     * @password is what the source selection carries and not what a countdown
     * was armed for: password-timeout defaults to 0, so keying this on a running
     * countdown would let every password through as cleartext on a stock
     * install. What the source is no longer carrying is dropped rather than
     * merely stepped over, this being one of the routes that would otherwise
     * find that record again and act on an exposure that is over. */
    g_paste_clipboards_manager_forget_stale_password (_from);

    if (_from->password && !_from->password_unconfirmed)
    {
        /* And never as bare text, as in notify_finish (): a refusal leaves the
         * destination selection alone rather than falling back to the very
         * exposure this is here to close. */
        if (g_paste_clipboards_manager_publish_item (_to, _from->password, FALSE))
        {
            /* Named for the copy it now carries, as notify_finish () names it for
             * the same write: this one is not independent either, so nothing else
             * moves the destination's serial, and a destination left naming an
             * older copy is one a synchronization made for that older copy is let
             * through over -- plain text over the password just published. None
             * can be out for one here, the source not being read (above), so this
             * is the invariant standing on its own rather than a window closed.
             * Never lowered: a destination already named for a newer copy stays
             * named for it, which is what notify_finish ()'s own skip leaves
             * standing there. */
            _to->change_serial = MAX (_to->change_serial, _from->change_serial);
            g_paste_clipboards_manager_arm_password (_to, _from->password, FALSE);
            _to->synced_generation = _to->generation;
        }
        else
        {
            g_debug ("clipboards-manager: the password was refused, not syncing it");
            /* That selection kept whatever it had, which is not this password. */
            g_paste_clipboards_manager_forget_stale_password (_to);
        }

        g_paste_clipboards_manager_finish_expiry (_to, NULL, CLIPBOARD_SECRET_UNKNOWN);
        return;
    }

    /* The source is carrying a secret this daemon holds no item for, so there is
     * nothing to copy over as one -- and copying it as text is the very exposure
     * the branch above refuses to fall back to. The destination keeps what it
     * has, minus a record that names a password it is not holding.
     *
     * A record its last read could not confirm (@password_unconfirmed) is refused
     * the same way rather than published as the item it names: that read saw
     * nothing, so the source may carry this password or a new copy, and only a
     * text read -- the very bare-text sync being refused -- could tell. */
    if (g_paste_clipboards_manager_may_carry_secret (_from))
    {
        g_debug ("clipboards-manager: not syncing a selection carrying an unrecorded secret");
        g_paste_clipboards_manager_forget_stale_password (_to);

        return;
    }

    /* Nothing is dropped for the destination here, unlike every other route that
     * publishes over a selection: the sync is a *read* of the source, and until
     * it lands the destination is still carrying exactly what its own records
     * name -- so asking now whether that selection has moved on can only ever
     * answer no, and a record retired now would be retired over a secret a
     * failed read leaves sitting there. The publishing is therefore ours and not
     * the backend's (sync_ready () below), which is what puts those records back
     * in reach at the one moment they are wrong: the moment the text lands. */
    GPasteClipboardsManagerUpdateData *data = g_paste_clipboards_manager_update_data_new (_to, FALSE);

    data->source = _from;
    data->source_generation = _from->generation;
    data->destination_generation = _to->generation;

    g_paste_clipboard_provider_sync_text (_from->clipboard, _to->clipboard,
                                          g_paste_clipboards_manager_sync_ready,
                                          data,
                                          (GDestroyNotify) g_paste_clipboards_manager_update_data_free);
}

/* Whether @clip's record is the copy a synchronization published there, with
 * nothing written or changed on the selection since: see @synced_generation. */
static gboolean
g_paste_clipboards_manager_is_synced_copy (_Clipboard *clip)
{
    return clip->password && clip->synced_generation && clip->synced_generation == clip->generation;
}

/* Take off the other selections any copy the synchronization made from @clip's
 * of a password @clip's selection no longer carries.
 *
 * Asked once a read of @clip's selection has settled everything it settles --
 * its record, its sensitivity, the synchronization it asked for -- and asked of
 * what that left, not of what the read found: a password removed by a release,
 * an empty text, a file list, a colour, an image, a format this daemon ignores,
 * another password, or any text with the synchronization off since, discovered
 * by a read of a change or only by an identifying read later; whichever it was,
 * the copy made of it would otherwise outlive the one it was made from, and
 * nothing else takes it off under a password-timeout of 0. The case this is for
 * is a password manager clearing what it copied, and ensure_not_empty ()
 * refusing to put a password back on the emptied selection closes only half of
 * it (the /clipboard/follow_release tests).
 *
 * A text the synchronization did carry over has replaced the copy already, and
 * its record with it, so nothing is left to find.
 *
 * Only a copy the synchronization made and nothing has touched since
 * (is_synced_copy ()): a password the user selected sits on every selection with
 * the same record, and is theirs to keep there whatever an owner does to one of
 * them, and what an owner put back is the owner's. Compared by value, not by
 * object, so a record converging on another object of the same password
 * (arm_password_at ()) is not taken for the password leaving. And only when both
 * selections are known: nothing read on either is outstanding and neither record
 * is unconfirmed, since a selection nobody identified might still carry the
 * password, and whatever else sits on the other one is the user's. The history
 * is left as it is: this is not an expiry, and retiring the head
 * (deselect_password ()) would have ensure_not_empty () fill an emptied
 * selection after all.
 *
 * One way round only, from the source's reads: a copy is judged by what its
 * source carries, and a read of the copy's own selection says nothing about that
 * source. Such a read cannot be out while the copy is still the
 * synchronization's either -- the publication superseded any, a change moves
 * @generation, and the daemon asks nothing of a selection it last wrote and
 * holds a confirmed record for. */
static void
g_paste_clipboards_manager_retire_synced_copies (_Clipboard *clip)
{
    if (clip->password_unconfirmed || g_paste_clipboard_provider_is_reading (clip->clipboard))
        return;

    const gchar *carried = (clip->password) ? g_paste_item_get_real_value (clip->password) : NULL;

    for (GSList *l = clip->manager->clipboards; l; l = g_slist_next (l))
    {
        _Clipboard *other = l->data;

        if (other == clip || !g_paste_clipboards_manager_is_synced_copy (other) ||
            g_paste_str_equal (g_paste_item_get_real_value (other->password), carried))
            continue;

        /* Cannot happen, as the function comment says: kept all the same, so
         * that nothing is published over a selection nobody identified, and said
         * aloud, since with nothing coming back to it the copy would otherwise
         * stay there for the rest of the session unremarked. */
        if (other->password_unconfirmed || g_paste_clipboard_provider_is_reading (other->clipboard))
        {
            g_warning ("A synchronized password copy could not be checked: its selection is %s",
                       (other->password_unconfirmed) ? "unidentified" : "being read");
            continue;
        }

        if (!g_paste_clipboards_manager_selection_holds (other->clipboard, other->password))
            continue;

        g_debug ("clipboards-manager: the password left its selection, taking its synchronized copy off");
        /* As in forget_stale_password (): retired through arm_password_at (),
         * which leaves the timeout edits waiting on that selection alone. */
        g_paste_clipboards_manager_arm_password_at (other, NULL, 0, 0, FALSE);
        g_paste_clipboards_manager_publish_over_password (other);
    }
}

static void
g_paste_clipboards_manager_notify_finish (_Clipboard           *clip,
                                          GPasteItem           *item,
                                          GPasteItem           *password,
                                          GPasteClipboardSecret secret,
                                          const gchar          *synchronized_text,
                                          gboolean              something_in_clipboard,
                                          guint64               change_serial)
{
    GPasteClipboardsManager *self = clip->manager;
    GPasteHistory *history = self->history;

    g_debug ("clipboards-manager: notify finish");

    /* A password read off a selection is on that selection already, and no other
     * route ever publishes it: its countdown starts here or it never starts.
     * Handed in rather than taken from @item, which the add below consumes and
     * which is not even there when the read was one we do not track -- the
     * exposure being the selection's and not the history's. */
    if (password)
        g_paste_clipboards_manager_arm_password (clip, password, FALSE);
    /* Whatever this read said about the selection, the standing record is only
     * retired once that selection has moved on from what it names -- which is
     * forget_stale_password ()'s question, and the one every other route here
     * asks. @sensitive is not that question and cannot stand in for it: it is a
     * hint that came back *and matched*, so a hint that never came back leaves
     * it false with the password still sitting there -- an owner re-asserting
     * the selection, a TARGETS conversion refused, a read the guard concluded
     * without it -- and dropping the record on that leaves the cleartext there
     * with nothing to take it off, which is the whole of what the record is for.
     *
     * The price is the one case a value comparison cannot call: the user copying
     * that very string as ordinary text keeps the record, and the countdown then
     * takes their own text back off. That is the side to be wrong on, a password
     * left on a selection outliving the session where a text taken off it is one
     * copy to make again. */
    else
        g_paste_clipboards_manager_forget_stale_password (clip);

    /* A secret this read could make no item of, which is an exposure with no
     * record: see @sensitive on _Clipboard. A failed text read can leave a
     * standing password record, so consult that record after checking whether
     * the selection still carries its value.
     *
     * Written afresh by every read rather than retired the way @password is:
     * there is no item to ask selection_holds () about, this read being the one
     * that produced none, so what a read says is the only answer available and
     * the newest read is the one that holds. The caveat is a read that comes
     * back without the hint over content that still carries it -- a TARGETS
     * conversion refused, or a refusal charged to a request already given up on
     * -- which reads as ordinary text and retires a record still owed. Nothing
     * here can tell that apart from a selection that really did move on, so the
     * read says which of the two it is: an answer retires the record, and
     * %CLIPBOARD_SECRET_UNKNOWN -- the selection never said what it offers, or
     * offered the hint and would not serve it -- leaves it exactly as it was.
     *
     * The record is read as a whole rather than matched against what this read
     * saw, there being nothing to match it with: a read that produced no item
     * brought back no value, so the same password copied twice and a different
     * secret whose text read failed look exactly alike here. What cannot be told
     * apart reads as "not a password" -- a secret is the corner case and nowhere
     * near the nominal one. The price is a record that may name the previous
     * password: a sync then publishes that one, masked and hinted and under a
     * countdown of its own, where the other way round would have every
     * unanswered read refuse syncs on selections carrying ordinary text.
     *
     * An empty payload makes no history item, but a matching hint still marks
     * the selection sensitive. Refusing a bare-text sync of it loses no usable
     * text and keeps this policy independent of history admission. */
    if (secret != CLIPBOARD_SECRET_UNKNOWN)
        clip->sensitive = (secret == CLIPBOARD_SECRET_YES) && !clip->password;

    /* History eviction can emit password-dropped synchronously and publish over
     * this selection, freeing the cache @synchronized_text borrows. A publication
     * also supersedes this copy, so it must not be synchronized afterwards. */
    guint64 generation = clip->generation;

    g_autoptr (GPasteItem) canonical_password = NULL;

    if (item)
    {
        g_paste_history_add (history, item);
        if (password && clip->generation == generation && clip->password == password)
        {
            canonical_password = g_paste_history_dup (history, 0);
            if (canonical_password && g_paste_item_equals (password, canonical_password))
            {
                /* A merged capture follows the surviving entry through renames
                 * and deletion, without restarting this selection's countdown. */
                g_set_object (&clip->password, canonical_password);
                password = canonical_password;
            }
        }
    }

    if (!something_in_clipboard)
        g_paste_clipboards_manager_ensure_not_empty (clip);

    if (synchronized_text && clip->generation == generation &&
        (password || secret == CLIPBOARD_SECRET_NO || !g_paste_clipboards_manager_may_carry_secret (clip)))
    {
        g_debug ("clipboards-manager: synchronizing clipboards");

        for (GSList *clipboard = self->clipboards; clipboard; clipboard = g_slist_next (clipboard))
        {
            _Clipboard *other = clipboard->data;

            /* Completion order is not copy order. Synchronization may retire an
             * older read, but must leave a newer external copy to conclude. */
            if (other == clip || other->change_serial > change_serial)
                continue;

            /* Copied over as the item it is when it is a password: bare text
             * reaches the other selection stripped of what says it is a secret,
             * and every clipboard manager watching that selection records it.
             *
             * Published even when that selection already holds the same string,
             * unlike ordinary text: what has to reach it is the hint, and the
             * bare text sitting there does not carry one -- the case where the
             * user selected the value with the mouse first and an app then
             * copied it as a secret. And never as bare text, so a refusal leaves
             * the selection alone rather than falling back to the very exposure
             * this is here to close; a countdown is armed only for a copy that
             * did reach it. */
            if (password)
            {
                if (g_paste_clipboards_manager_publish_item (other, password, FALSE))
                {
                    /* Named for this copy exactly as the text branch below does
                     * it: the write is not independent, so nothing else moves
                     * the destination's serial, and one left naming an older
                     * copy is one a sync completing out of order may write over
                     * -- plain text over the password just published. */
                    other->change_serial = change_serial;
                    g_paste_clipboards_manager_arm_password (other, password, FALSE);
                    other->synced_generation = other->generation;
                }
                else
                {
                    g_debug ("clipboards-manager: the password was refused, not synchronizing it");
                    /* That selection kept whatever it had, which is not this
                     * password: a record left saying otherwise is the stale one
                     * every route below reads. */
                    g_paste_clipboards_manager_forget_stale_password (other);
                }
            }
            else
            {
                const gchar *text = g_paste_clipboard_provider_get_text (other->clipboard);

                /* A selection carrying an unrecorded secret is written to rather
                 * than compared. What get_text () answers is the last text read
                 * that landed, which on a sensitive selection is exactly what
                 * did not: a hint that came back and matched beside a text read
                 * that failed leaves the cache naming an older string while the
                 * secret sits there. Comparing against that cache would find the
                 * strings equal, write nothing, and clear a record over the
                 * cleartext it was kept for -- after which a sync out of that
                 * selection publishes the secret as bare text. So the one case
                 * where the daemon writes nothing and knows what sits there all
                 * the same is the case where the record already says it does not.
                 * See @sensitive on _Clipboard. */
                if (other->sensitive || !text || !g_paste_str_equal (text, synchronized_text))
                {
                    other->change_serial = change_serial;
                    g_paste_clipboards_manager_publish_text (other, synchronized_text, FALSE);
                }

                g_paste_clipboards_manager_forget_stale_password (other);
            }
        }

        /* Expiry can publish a replacement across both selections. Finish it
         * after the loop, once no iteration borrows the source's cached text. */
        g_paste_clipboards_manager_finish_expiry_all (self);
    }

    g_paste_clipboards_manager_retire_synced_copies (clip);
}

static void
g_paste_clipboards_manager_update_ready (GPasteClipboardProvider *clipboard,
                                         GPasteItem              *item,
                                         gboolean                 superseded,
                                         GPasteClipboardSecret    secret,
                                         gpointer                 user_data)
{
    g_autoptr (GPasteClipboardsManagerUpdateData) data = user_data;
    _Clipboard *clip = data->clip;

    g_debug ("clipboards-manager: update ready");

    if (!superseded)
    {
        g_paste_clipboards_manager_confirm_password (clip, secret);
        g_paste_clipboards_manager_preserve_password (clip, &item, secret);
    }

    /* As in identify_ready (): resolve any remaining expiry before dropping
     * stale results or results arriving after explicit disposal. */
    if (g_paste_clipboards_manager_finish_expiry (data->clip, &item, secret) || superseded || !data->manager->history)
    {
        g_clear_object (&item);
        g_paste_clipboards_manager_resume_sync (clip);
        return;
    }

    guint64 change_serial = data->change_serial;

    g_paste_clipboards_manager_finish_refresh (clip, &item, secret);

    const gchar *synchronized_text = NULL;

    if (item && g_paste_clipboard_provider_get_text (clipboard) &&
        g_paste_settings_get_synchronize_clipboards (clip->manager->settings))
        synchronized_text = g_paste_clipboard_provider_get_text (clipboard);

    /* Held past the drop below and past the add that consumes it: a password is
     * on the selection whether or not it is one the history takes, so the copy
     * made of it has to carry what says it is a secret, and both selections
     * still need the countdown that takes it back off. */
    g_autoptr (GPasteItem) password = (item && G_PASTE_IS_PASSWORD_ITEM (item)) ? g_object_ref (item) : NULL;

    if (!data->track && item)
        g_clear_object (&item);

    gboolean something_in_clipboard = !!g_paste_clipboard_provider_get_text (clipboard) ||
                                      !!g_paste_clipboard_provider_get_image_checksum (clipboard);

    g_paste_clipboards_manager_notify_finish (clip, item, password, secret, synchronized_text, something_in_clipboard, change_serial);
    g_paste_clipboards_manager_resume_sync (clip);
}

/* FIXME: the GDK backend costs two of these per external copy, where one copy is
 * one change as far as everything below is concerned.
 *
 * It raises a change for the empty formats a new owner is announced with and
 * another for that owner's targets, and both arrive here: each bumps
 * @generation and @change_serial, resets @refresh_read_after and allocates an
 * update, and the second supersedes the formats wait the first left out, which
 * spends an idle delivering a callback with nothing in it. Nothing is wrong by
 * it -- both bumps sit inside one genuine owner change, the gap between them
 * hides no request from anything since is_reading () keeps the cache shut across
 * it, and @change_serial orders copies rather than counting them, so twice the
 * rate says nothing different. What it costs is two allocations and a GSource
 * per copy, doubled on the primary selection, which changes with every mouse
 * selection.
 *
 * Two ways out, of which only one is this file's:
 *
 * - Coalescing here, the second change reusing the first's update data and
 *   leaving the serials where they are. It asks the manager to know that two
 *   changes can be one copy, which is true of GTK4 and of nothing else -- the
 *   mutter backend raises one -- so it would put a toolkit's quirk in the layer
 *   that exists not to carry one. Not this one.
 *
 * - One change per copy out of the GDK backend, which holds the empty one and
 *   emits when the targets arrive or its wait concludes. The pairing is that
 *   backend's to know, so this is the right shape, and it is not a small change:
 *   the wait is there to answer the callback this function hands it, so with no
 *   first notification it has nobody to answer, and its conclusion would have to
 *   raise a change that update () reads from the concluded state instead of
 *   taking for a fresh set of empty formats.
 *
 * Postponed rather than taken with the work around it: that second one is a
 * redesign of the path every copy, synchronization and password countdown goes
 * through, and the reasoning below that leaves this selection's pending strips
 * alone across a change is written against seeing both of them -- it has to be
 * re-derived the day one change per copy arrives, not carried over
 * (/clipboard/strip_during_read/superseded). What it buys is allocations. */
static void
g_paste_clipboards_manager_notify (GPasteClipboardProvider *clipboard,
                                   gpointer                 user_data)
{
    _Clipboard *clip = user_data;

    ++clip->generation;
    clip->refresh_read_after = 0;

    g_debug ("clipboards-manager: notify");

    /* The strips this selection is owed are left where they are. They are spent
     * by the read that concludes, which is what asks whether the history still
     * holds each one and whether the selection still carries it
     * (finish_refresh ()), and a change is not an answer to either: the GDK
     * backend raises two of these per external copy -- the empty formats a new
     * owner is announced with, then its targets -- so a request dropped here is
     * one lost to a change that never happened, with the history already stripped
     * and the selection still serving the rich flavours
     * (/clipboard/strip_during_read/superseded). */
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

/* Forget the password @clip's selection was carrying once it stops carrying it.
 *
 * The record is what every route asking whether that selection holds a secret
 * reads, so one left behind has them act on an exposure that is over:
 * sync_from_to () publishing a password item the source does not hold -- masked
 * in the history, hinted on the wire, and under a countdown that then takes the
 * user's own text back off -- and rearm_password () handing more time to a
 * password that has already left.
 *
 * The provider's cache is what says the selection moved, not the update having
 * produced something: a text deduped against the one already cached produces
 * nothing precisely because the selection is standing still, where one the size
 * policy turns down produces nothing while the selection has moved -- which is
 * why that one leaves the cache holding no value at all. */
static void
g_paste_clipboards_manager_forget_stale_password (_Clipboard *clip)
{
    /* Not while the record is unconfirmed: the cache holding no value is then a
     * read that saw nothing, not a selection that moved (@password_unconfirmed).
     *
     * Dropped through arm_password_at () rather than arm_password (%NULL): what
     * is known here is that the selection stopped carrying the password this
     * record names, which says nothing about the timeout edits waiting for that
     * selection to be identified (@pending_passwords) -- and arm_password ()
     * keeps only the edit matching the item it is arming, so a retirement
     * through it would take every other one with it. Resolving them is
     * finish_expiry ()'s, on the next read or publication that does identify the
     * selection; the one retirement that may drop them outright is clear ()'s,
     * which publishes over the selection first and so answers the very question
     * those edits are waiting on. */
    if (clip->password && !g_paste_clipboard_provider_is_reading (clip->clipboard) &&
        !clip->password_unconfirmed &&
        !g_paste_clipboards_manager_selection_holds (clip->clipboard, clip->password))
        g_paste_clipboards_manager_arm_password_at (clip, NULL, 0, 0, FALSE);
}

/* Retiring the history's active password does not establish ownership of any
 * selection. Move the fallback without publishing it, even while reads are
 * pending, so a flush followed by a new daemon cannot restore the expired
 * head. */
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

/* Record @item as what @clip's selection carries, and arm the countdown it needs.
 *
 * The record is kept whether or not there is a countdown to arm: a timeout of 0
 * means the password stays until something else replaces it, which is a fact
 * about the selection and not the absence of one. Only the countdown is
 * conditional.
 *
 * Every route that puts an item on a selection ends here -- select () for the
 * history's own signal and for the add path, ensure_not_empty () for a provider
 * re-owning a selection it found empty, rearm_password () for a timeout written
 * under a countdown already running, and notify_finish () and identify_ready ()
 * for the two that publish nothing: a password read *off* a selection, marked as
 * a secret by whoever put it there, is on that selection before the daemon has
 * heard of it -- already there when it started, in the bootstrap read's case.
 * A password reaching a selection any other way sits on it with nothing to take
 * it off. link_captures () re-arms such a bootstrap capture once the history has
 * loaded, and reads nothing itself.
 *
 * An @item that is not a password -- %NULL included, which is what
 * forget_stale_password () and clear () pass -- is this selection moving on from
 * whatever it was carrying, so the record and its countdown go rather than being
 * replaced.
 *
 * The one write that skips it is the one taking a password back off:
 * deselect_password () putting the replacement or the empty string there, which
 * picks the first item that is not a password and so has no countdown to arm. */
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
     * already armed for that MakePassword can change under it.
     *
     * Asked of the value and not of the object, an exposure being a cleartext
     * sitting on a selection: a password read off one is merged into the head it
     * matched and dropped there (g_paste_history_private_merge_into_head ()), so
     * the record and the item every later route hands back -- rearm_password ()
     * above all -- are two objects carrying the one cleartext. The record takes
     * the object it was just handed, which is how it converges on the one the
     * history kept. */
    if (!changed && clip->password && timeout == clip->password_timeout && G_PASTE_IS_PASSWORD_ITEM (item) &&
        g_paste_str_equal (g_paste_item_get_real_value (item), g_paste_item_get_real_value (clip->password)))
    {
        g_set_object (&clip->password, item);
        /* As on the way through below: arming is this selection being identified
         * as carrying @item, which is the very thing an unconfirmed record is
         * waiting to hear. Every caller that identified the selection has cleared
         * the flag already -- confirm_password () or note_published () runs
         * before each of them -- so this is what keeps that a fact about the
         * function rather than about its callers. The one caller that identified
         * nothing, link_captures (), puts the flag back itself. A record left
         * unconfirmed here is one forget_stale_password ()
         * never retires, sync_from_to () refuses every sync out of, and
         * expire_password_finish () fails a re-exec over. */
        clip->password_unconfirmed = FALSE;

        return;
    }

    /* Whatever this selection was carrying is what @item has just replaced on it,
     * and no other selection's countdown is any of this one's business. */
    g_clear_handle_id (&clip->password_timeout_id, g_source_remove);
    clip->password_expired = FALSE;
    clip->password_timeout = 0;
    clip->password_unconfirmed = FALSE;
    clip->link_on_load = FALSE;

    /* An item that is not a password is the selection moving on from one.
     * Written with g_set_object () rather than a clear and a ref of our own: the
     * item already recorded here is what rearm_password () hands back under a
     * duration that moved, and letting go of the record's reference before
     * taking one would be letting go of the last reference on the very item
     * about to be ref'd -- the callers hold it borrowed. */
    GPasteItem *password = (G_PASTE_IS_PASSWORD_ITEM (item)) ? item : NULL;

    g_set_object (&clip->password, password);
    clip->armed_from = deadline - (gint64) timeout * G_USEC_PER_SEC;

    if (!password || !timeout)
        return;

    /* Read once: a second reading past @deadline would make the remainder below
     * negative, which wraps to about 49 days as a guint. */
    gint64 now = g_get_monotonic_time ();

    clip->password_timeout = timeout;
    if (deadline <= now)
    {
        clip->password_expired = TRUE;
        return;
    }

    /* Armed for what is left until @deadline rather than for @timeout: an edit
     * resolved after a read kept the deadline it was made under, and a whole
     * duration handed out again there would be exposure the user did not ask
     * for. Milliseconds, since that remainder is not a round number of
     * seconds. */
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

        if (G_PASTE_IS_PASSWORD_ITEM (item) &&
            g_paste_str_equal (g_paste_item_get_real_value (change->item), g_paste_item_get_real_value (item)))
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

    if (!item)
        return;

    /* As in publish_item (), which this is the one publishing route not to go
     * through, the provider having picked what to put there itself. */
    g_paste_clipboards_manager_note_published (clip);

    g_paste_clipboards_manager_arm_password (clip, item, FALSE);
    g_paste_clipboards_manager_finish_expiry (clip, NULL, CLIPBOARD_SECRET_UNKNOWN);
}

/* Ask a selection whose record went unconfirmed what it carries.
 *
 * A record the last read could not confirm (@password_unconfirmed on
 * _Clipboard) has nothing coming to resolve it: the read that would have has
 * already answered, and the next one waits on a copy the user may never make. A
 * read is what identifies a selection, so one is asked for here rather than
 * waited for.
 *
 * Answered by identify_ready (), which is what keeps it a question: nothing is
 * tracked out of it, no copy serial moves and nothing reaches the other
 * selection. A read already out is the answer on its way, and is left alone. */
static void
g_paste_clipboards_manager_reread_unconfirmed_clip (_Clipboard *clip)
{
    if (!clip->password_unconfirmed || g_paste_clipboard_provider_is_reading (clip->clipboard))
        return;

    g_debug ("clipboards-manager: re-reading a selection whose record went unconfirmed");
    g_paste_clipboards_manager_identify (clip);
}

/* Take the password @clip's countdown was armed for back off its selection, if
 * that selection still carries it. One that has moved on holds what replaced it,
 * which is the user's and not ours to overwrite. History retirement is separate:
 * an expired head must not be restored when an unrelated selection falls
 * empty. */
static void
g_paste_clipboards_manager_deselect_password (_Clipboard *clip)
{
    GPasteClipboardsManager *self = clip->manager;

    g_clear_handle_id (&clip->password_timeout_id, g_source_remove);

    if (clip->password)
        g_paste_clipboards_manager_retire_password (self, clip->password);

    /* Overdue rather than taken off, both while a read is out and while the last
     * one could not confirm the record: neither says what sits on the selection,
     * and the read that does is what finish_expiry () acts on. */
    if (clip->password && (g_paste_clipboard_provider_is_reading (clip->clipboard) || clip->password_unconfirmed))
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
    g_paste_clipboards_manager_publish_over_password (clip);
}

/* Put something other than a password on @clip's selection, which carries one
 * whose record has already been dropped: the newest item of the history that is
 * not a password, or the empty string. */
static void
g_paste_clipboards_manager_publish_over_password (_Clipboard *clip)
{
    GPasteClipboardsManager *self = clip->manager;

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
    if (!replacement || !g_paste_clipboards_manager_publish_item (clip, replacement, FALSE))
        g_paste_clipboards_manager_publish_text (clip, "", FALSE);
}

static void
g_paste_clipboards_manager_on_password_timeout (gpointer user_data)
{
    _Clipboard *clip = user_data;

    /* This is the source firing, so it is spent: drop the id before anything can
     * reach for g_source_remove () on it. */
    clip->password_timeout_id = 0;
    g_paste_clipboards_manager_deselect_password (clip);

    /* Marked overdue rather than taken off, which leaves the deadline waiting on
     * a read that identifies the selection. With one out that read is on its
     * way; with none, the next is a copy the user may never make, and the
     * password would sit there for the rest of the session. So it is asked for
     * here -- the countdown running out being the one moment the deadline is
     * owed an answer -- and the read that comes back clears the record's doubt
     * and reaches finish_expiry (), which is where the deadline is spent. */
    if (clip->password_expired)
        g_paste_clipboards_manager_reread_unconfirmed_clip (clip);
}

/**
 * g_paste_clipboards_manager_expire_password:
 * @self: a #GPasteClipboardsManager instance
 *
 * Request password expiry without waiting for the configured timeout
 *
 * Meant for the paths where there is no later to wait for: a daemon standing down
 * leaves the selections behind it, so a password under a countdown would sit
 * there for the rest of the session with nothing left to take it back off.
 *
 * A countdown is what this brings forward, and a selection running none has
 * nothing to bring: password-timeout defaults to 0, which is a password the user
 * asked to keep for as long as anything else -- taking it off here would be
 * choosing, on a stock install, to replace whatever they had just copied. The
 * record kept for such a password says what the selection carries (see
 * arm_password ()) and is not a deadline anything may act on.
 *
 * A pending read retains cleanup until its selection is known, even across
 * disposal. A caller stopping the main loop must await the async variant.
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_expire_password (GPasteClipboardsManager *self)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));

    for (GSList *clipboard = self->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        _Clipboard *clip = clipboard->data;

        /* A countdown is what this brings forward, so a record running none is
         * left exactly as it is -- and its item stays where it is in the
         * history, nothing being about to take it off any selection. */
        clip->force_expiry = clip->password_timeout || clip->password_expired || clip->pending_passwords;
        if (clip->password && (clip->password_timeout || clip->password_expired))
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

/* Every selection whose record went unconfirmed, asked at once: this is the
 * waiter's use of the above. The wait would otherwise report a failure that
 * every later request reports just as surely, so `gpaste-client daemon-reexec`
 * and the migration that goes through it refuse for as long as the selection
 * stands still. */
static void
g_paste_clipboards_manager_reread_unconfirmed (GPasteClipboardsManager *self)
{
    for (GSList *l = self->clipboards; l; l = l->next)
        g_paste_clipboards_manager_reread_unconfirmed_clip (l->data);
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
     * list holds the task, and a manager holding itself is one whose dispose ()
     * -- one of the things that answers this task -- can never run. The callback
     * is handed the manager by whatever it kept a reference to instead. */
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

    /* Before the expiry below, so a selection being re-read is one is_reading ()
     * holds the wait open for rather than one it concludes over. */
    g_paste_clipboards_manager_reread_unconfirmed (self);
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
 * %G_IO_ERROR_TIMED_OUT, a deadline failure, must not lead to a restart. And in
 * %G_PASTE_ERROR with %G_PASTE_ERROR_FAILED for a selection no read could
 * identify, which is a failure of this daemon's own rather than of the transport
 * the answer travels back over (see return_expiry_on_idle()).
 *
 * The task carries no source object (see expire_password_async()), so @self is
 * the caller saying which manager it asked rather than something read back off
 * @result.
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

        /* An unanswered read leaves the same identity question outstanding
         * after I/O ends. Keep edits until a read or publication resolves it. */
        if (g_paste_clipboard_provider_is_reading (clip->clipboard) || clip->password_unconfirmed)
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
                /* The duration alone: arm_password_at () already tells another
                 * password apart by value, and comparing objects would take a
                 * rename of the record's same-value twin for an edit. */
                change->changed = timeout != clip->password_timeout;
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

        /* As in sync_from_to (): this is one of the routes that reads the record
         * below, so one the selection has moved on from goes before it is asked.
         * A strip refused down there is refused by nothing at all -- neither
         * published, nor queued, nor kept as refused -- so the record answering
         * for the selection has to be one that still names what it carries. */
        g_paste_clipboards_manager_forget_stale_password (clip);

        /* A hidden cache can be a pending or inconclusive read. The next answer
         * checks the selection before publishing; completed I/O alone cannot
         * spend a strip while its secrecy remains unresolved. */
        if (g_paste_clipboard_provider_is_reading (clip->clipboard) || clip->password_unconfirmed || clip->sensitive)
        {
            g_paste_clipboards_manager_queue_refresh (clip, item);
            gint64 now = g_get_monotonic_time ();

            if (!g_paste_clipboard_provider_is_reading (clip->clipboard) && now >= clip->refresh_read_after)
            {
                clip->refresh_read_after = now + G_USEC_PER_SEC;
                g_paste_clipboards_manager_identify (clip);
            }
        }
        /* A value match does not authorize stripping a password's hint. As in
         * finish_refresh (), classification takes precedence over the text. */
        else if (!g_paste_clipboards_manager_may_carry_secret (clip) &&
                 g_paste_clipboards_manager_selection_holds (clip->clipboard, item))
        {
            if (!g_paste_clipboards_manager_publish_item (clip, item, FALSE))
                g_paste_clipboards_manager_refuse_refresh (clip, item);
        }
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

        if (!g_paste_clipboards_manager_publish_item (clip, item, TRUE))
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

/* Empty every selection: a head removal left a password that nobody asked to
 * publish, while what was removed may still be sitting there. As in
 * deselect_password (), the empty string is how a selection is emptied.
 *
 * Every selection, and not only the ones still carrying what was removed. This
 * is the removal's own publication -- the one select () makes for every other
 * new head, over whatever each selection happened to hold -- with the head it
 * would have published withheld because it is a password. Asking each selection
 * what it holds first would make a head removal reassign the selections or not
 * depending on which of the two the new head turned out to be. The guarded write
 * is deselect_password ()'s, and for the opposite reason: a countdown running
 * out is nobody asking for anything just then. */
static void
g_paste_clipboards_manager_clear (GPasteClipboardsManager *self)
{
    for (GSList *clipboard = self->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        _Clipboard *clip = clipboard->data;

        g_paste_clipboards_manager_publish_text (clip, "", TRUE);
        g_paste_clipboards_manager_arm_password (clip, NULL, FALSE);
    }

    /* As in select (): the publication identified every selection. */
    g_paste_clipboards_manager_finish_expiry_all (self);
}

/* A capture identify_ready () could not link to the history's head, for want of
 * one, is linked once the history has loaded.
 *
 * g_paste_daemon_new () starts both bootstrap reads before the history load, and
 * nothing orders the two: the mutter backend has the formats at once, so its
 * read can conclude before the saver has read anything back. The capture then
 * finds no head, and without this its countdown would run from password-timeout
 * rather than from the timeout stored on the entry it is a copy of -- which is
 * the one thing linking is for (/clipboard/bootstrap_before_load).
 *
 * Nothing is tried while the load is still in flight (g_paste_history_is_loading ()):
 * an add made in the meantime shows a head too, but the load throws it away and
 * installs the one a read would have linked against, so trying that add would
 * spend the link on a head that is about to disappear
 * (/clipboard/bootstrap_before_load/add_during_load). Once loaded, the first head
 * seen is tried once: a copy made since is that head, and simply does not match.
 *
 * The link is the one identify_ready () would have made, with two things kept as
 * they were:
 *
 * - the moment the countdown started (@armed_from): the head's timeout counts from
 *   the capture, not from now, so the load taking its time does not buy the
 *   password more exposure. A deadline already behind us is the countdown running
 *   out, and is handled as that.
 * - @password_unconfirmed: arm_password_at () clears it, every other caller
 *   having just identified the selection, but this one read nothing -- a record a
 *   later read could not confirm is no more confirmed for the history having
 *   loaded, and clearing it would let the next sync retire the record over a
 *   selection nobody identified (/clipboard/bootstrap_before_load/unconfirmed).
 *
 * Whether the record is a synchronized copy needs no such care: a re-arm moves
 * no @generation (see @synced_generation). */
static gboolean
g_paste_clipboards_manager_link_captures (gpointer user_data)
{
    g_autoptr (GPasteClipboardsManager) self = g_weak_ref_get (user_data);

    if (!self)
        return G_SOURCE_REMOVE;

    self->link_source_id = 0;

    if (!self->history || g_paste_history_is_loading (self->history))
        return G_SOURCE_REMOVE;

    g_autoptr (GPasteItem) head = g_paste_history_dup (self->history, 0);

    if (!head)
        return G_SOURCE_REMOVE;

    for (GSList *l = self->clipboards; l; l = g_slist_next (l))
    {
        _Clipboard *clip = l->data;

        if (!clip->link_on_load)
            continue;

        clip->link_on_load = FALSE;

        if (!clip->password || head == clip->password || !G_PASTE_IS_PASSWORD_ITEM (head) ||
            !g_paste_item_equals (clip->password, head))
            continue;

        guint timeout = g_paste_clipboards_manager_password_timeout (head);
        gboolean unconfirmed = clip->password_unconfirmed;

        g_debug ("clipboards-manager: linking a startup capture to the history loaded after it");
        g_paste_clipboards_manager_arm_password_at (clip, head, timeout, clip->armed_from + (gint64) timeout * G_USEC_PER_SEC, FALSE);
        clip->password_unconfirmed = unconfirmed;
        /* Handled as the countdown running out, and so as on_password_timeout ()
         * handles it: an unconfirmed record is only marked overdue there, and
         * the read that identifies the selection is asked for rather than
         * waited on -- nothing else is owed (/clipboard/bootstrap_before_load/overdue). */
        if (clip->password_expired)
            g_paste_clipboards_manager_on_password_timeout (clip);
    }

    return G_SOURCE_REMOVE;
}

/* Deferred to an idle: ::update is emitted with the history's lock held, and
 * reading the head back from here would take it again. */
static void
on_history_update (GPasteClipboardsManager *self,
                   GPasteUpdateAction       action,
                   GPasteUpdateTarget       target,
                   const gchar             *uuid G_GNUC_UNUSED,
                   guint64                  index G_GNUC_UNUSED,
                   GPasteHistory           *history G_GNUC_UNUSED)
{
    if (action != G_PASTE_UPDATE_ACTION_REPLACE || target != G_PASTE_UPDATE_TARGET_ALL || self->link_source_id)
        return;

    for (GSList *l = self->clipboards; l; l = g_slist_next (l))
    {
        if (((_Clipboard *) l->data)->link_on_load)
        {
            self->link_source_id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, g_paste_clipboards_manager_link_captures,
                                                    g_paste_weak_ref_new (self), g_paste_weak_ref_free);
            g_source_set_name_by_id (self->link_source_id, "[GPaste] link startup captures");
            return;
        }
    }
}

static void
on_item_selected (GPasteClipboardsManager *self,
                  GPasteItem              *item,
                  GPasteHistory           *history G_GNUC_UNUSED)
{
    if (!item)
        g_paste_clipboards_manager_clear (self);
    else if (!g_paste_clipboards_manager_select (self, item))
        g_paste_history_remove (self->history, 0);
}

/* A password's entry has left the history, so nothing records the cleartext it
 * may still be sitting on and nothing would ever take it off: a password-timeout
 * of 0 runs no countdown, and the entry that went was the last thing saying the
 * exposure happened. The selections recorded as carrying it give it up,
 * which is deselect_password ()'s answer to the same question -- it puts the first
 * item that is not a password there, or empties the selection when the history has
 * none left (the /clipboard/dropped_password tests).
 *
 * Only the selections whose record names it, unlike clear (): this is not a new
 * head being published over everything, and an unrelated copy is the user's own.
 * Which is also why this cannot be decided where the entry goes -- the history
 * knows what left, and only the manager knows what each selection carries. */
static void
on_password_dropped (GPasteClipboardsManager *self,
                     GPasteItem              *item,
                     GPasteHistory           *history G_GNUC_UNUSED)
{
    gboolean dropped = FALSE;

    for (GSList *l = self->clipboards; l; l = l->next)
    {
        _Clipboard *clip = l->data;

        /* Named passwords can share a value while remaining distinct entries.
         * A record whose own entry survives is not the password being dropped.
         * Unlisted capture/merge twins are compared below. */
        if (clip->password && g_paste_history_get_by_uuid (self->history, g_paste_item_get_uuid (clip->password)))
            continue;

        /* Capture/merge twins are equal nameless items. Named entries remain
         * distinct even when their cleartext matches an unlisted twin. */
        if (clip->password &&
            g_paste_item_equals (clip->password, item))
        {
            g_paste_clipboards_manager_deselect_password (clip);
            dropped = TRUE;
        }
    }

    /* As in clear (): the take-off identified every selection it reached, so the
     * timeout edits waiting on one of them can be resolved. */
    if (dropped)
        g_paste_clipboards_manager_finish_expiry_all (self);
}

static void
_clipboard_free (gpointer data)
{
    _Clipboard *clip = data;

    g_clear_handle_id (&clip->password_timeout_id, g_source_remove);
    g_clear_object (&clip->password);
    g_clear_pointer (&clip->refused_refresh, g_paste_weak_ref_free);
    g_clear_slist (&clip->pending_passwords, pending_password_free);
    g_clear_slist (&clip->pending_refreshes, g_paste_weak_ref_free);
    g_clear_object (&clip->signal_group);
    g_object_unref (clip->clipboard);
    g_free (clip);
}

static void
g_paste_clipboards_manager_dispose (GObject *object)
{
    GPasteClipboardsManager *self = G_PASTE_CLIPBOARDS_MANAGER (object);

    /* Before anything is released: deselecting goes through the history and both
     * providers, and a countdown nothing will be left to run is one to bring
     * forward rather than drop. */
    g_paste_clipboards_manager_expire_password (self);

    /* Explicit disposal ends waiters; the outstanding reads retain cleanup. */
    g_paste_clipboards_manager_return_expiry (self);

    for (GSList *l = self->clipboards; l; l = l->next)
    {
        _Clipboard *clip = l->data;

        g_clear_slist (&clip->pending_refreshes, g_paste_weak_ref_free);
        g_clear_pointer (&clip->refused_refresh, g_paste_weak_ref_free);
        g_clear_object (&clip->signal_group);
    }

    g_clear_handle_id (&self->link_source_id, g_source_remove);
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
    g_signal_group_connect_swapped (history_signals, "password-dropped", G_CALLBACK (on_password_dropped), self);
    g_signal_group_connect_swapped (history_signals, "update", G_CALLBACK (on_history_update), self);
    g_signal_group_set_target (history_signals, history);

    return self;
}
