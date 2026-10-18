// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-error.h>
#include <gpaste-3/gpaste-gsettings-keys.h>
#include <gpaste-3/gpaste-update-enums.h>
#include <gpaste-3/gpaste-util.h>

#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-history.h>
#include <gpaste-daemon/gpaste-history-saver.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-uris-item.h>

#include <gio/gio.h>

/* Takes the history lock for the rest of the scope */
#define G_PASTE_DO_LOCK_HISTORY                 \
    g_debug ("%s: Locking history", G_STRFUNC); \
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&self->lock)

/* Same, and emits the pending SELECTED signal, if any, once the lock has been
 * released again: the guard is declared before the locker so that its cleanup
 * runs after the unlock.
 */
#define G_PASTE_LOCK_HISTORY                                                              \
    g_auto (GPasteHistorySelectionScope) selection_scope = { .self = self };              \
    G_PASTE_DO_LOCK_HISTORY

struct _GPasteHistory
{
    GObject parent_instance;

    GMutex                lock;

    GPasteStorageBackend *backend;
    GPasteHistorySaver   *saver;
    GPasteSettings       *settings;
    GSignalGroup         *settings_signals;

    /* The model: newest first, holding a ref on each item, plus an index from
     * uuid to the very same items. The hash borrows both — the key is the
     * item's own uuid string and the value is the item the array owns — so an
     * entry is only ever valid while the item is in the array, and the two are
     * updated together. That is safe because a uuid never changes once its item
     * is in the history: g_paste_item_set_uuid() is only called by the storage
     * backends while loading, and on an item about to take another's place
     * under its uuid, before either gets here. */
    GPtrArray            *history;
    GHashTable           *by_uuid;
    gsize                 size;
    guint64               passwords; /* among the items: see g_paste_history_stored_length () */

    gchar                *name;

    /* Set once the history has been flushed for shutdown/handover: no further
     * change is persisted, so a successor daemon owns the on-disk state. Only
     * the deliberate counterpart of that handover lifts it (g_paste_history_resume,
     * or reload_backend once a migration has rewritten the store) — never the
     * mere start of a load, which can happen throughout a handover window that
     * keeps running (the in-shell daemon stays on the bus across its migration
     * dialog, so a history switch can land right in the middle of it). */
    gboolean              stopped;

    /* Set when *this* history is on disk but could not be read back, so the
     * empty model we ended up with is never written over the intact data.
     * Unlike @stopped it belongs to the loaded history alone: starting another
     * load clears it, and that load's own result sets it again. */
    gboolean              unreadable;

    /* What the memory cap evicts next: the biggest item it may evict at all,
     * which is never the first (active) one nor a favourite, and %NULL when
     * there is none. Kept up to date as the history changes, which spares the
     * cap a scan for its first eviction; the price is that every path moving an
     * item to or from the head, resizing, pinning or replacing one has to keep
     * it right, or re-elect. A stale one has the cap evict an item it must not,
     * or compare against an item already freed, @biggest_uuid borrowing its
     * string from the item it names. */
    const gchar          *biggest_uuid;
    guint64               biggest_size;

    /* Recorded by g_paste_history_selected and emitted by G_PASTE_LOCK_HISTORY
     * once the lock is released again. @pending_clear is a selection recorded
     * with no item: see g_paste_history_remove_common (). @pending_dropped holds
     * the passwords that left the model, ref'd because what announces them is
     * also what frees them (g_paste_history_private_remove ()). */
    GPasteItem           *pending_selection;
    gboolean              pending_clear;
    GSList               *pending_dropped;

    /* The uuids of what was added while @stopped: in the model and nowhere
     * else, recording having stopped, until the reload ending the handover
     * carries it over (g_paste_history_reload_backend ()). Uuids rather than
     * the items, so that what a cap evicts meanwhile is freed. */
    GHashTable           *stopped_adds;

    /* The passwords the model held when reload_backend () swapped it out, until
     * the reloaded model arrives: one it no longer holds -- a migration to a
     * plain flavour imports none -- has left the history, and is announced as
     * any other departure is. A switch starting another load drops them: that
     * is a history swapped out, which no password leaves. */
    GSList               *reload_passwords;
};

G_PASTE_DEFINE_TYPE (History, history, G_TYPE_OBJECT)

enum
{
    SELECTED,
    PASSWORD_DROPPED,
    SWITCH,
    UPDATE,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef struct
{
    GPasteHistory *self;
} GPasteHistorySelectionScope;

static void
g_paste_history_emit_pending (GPasteHistorySelectionScope *scope)
{
    GPasteHistory *self = scope->self;
    g_autoptr (GPasteItem) item = NULL;
    g_autoslist (GPasteItem) dropped = NULL;
    gboolean clear;

    {
        G_PASTE_DO_LOCK_HISTORY;

        item = g_steal_pointer (&self->pending_selection);
        clear = self->pending_clear;
        self->pending_clear = FALSE;
        dropped = g_steal_pointer (&self->pending_dropped);
    }

    if (item || clear)
    {
        g_debug ("history: selected");

        g_signal_emit (self,
                       signals[SELECTED],
                       0, /* detail */
                       item,
                       NULL);
    }

    /* After the selection, which publishes over every selection and drops the
     * records with it: a password that went as the head is then announced to a
     * manager with nothing left to take off. */
    for (GSList *l = dropped; l; l = l->next)
    {
        g_debug ("history: password dropped");

        g_signal_emit (self,
                       signals[PASSWORD_DROPPED],
                       0, /* detail */
                       l->data,
                       NULL);
    }
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (GPasteHistorySelectionScope, g_paste_history_emit_pending)

/* @spared is left out of the running too, for the one change it was just the
 * subject of: see _g_paste_history_replace (). */
static void
g_paste_history_private_elect_new_biggest_sparing (GPasteHistory *self,
                                                   GPasteItem    *spared)
{
    g_debug ("history: elect biggest");

    self->biggest_uuid = NULL;
    self->biggest_size = 0;

    /* From 1: the first (active) item is never tracked. Favourites are never
     * tracked either: this elects what the memory cap may evict, and evicting a
     * favourite is precisely what being one rules out. With nothing electable
     * biggest_uuid stays %NULL, which is what stops the cap's loop. */
    for (guint i = 1; i < self->history->len; ++i)
    {
        GPasteItem *item = g_ptr_array_index (self->history, i);

        if (item == spared || g_paste_item_is_favourite (item))
            continue;

        guint64 size = g_paste_item_get_size (item);

        if (size > self->biggest_size)
        {
            self->biggest_uuid = g_paste_item_get_uuid (item);
            self->biggest_size = size;
        }
    }
}

static void
g_paste_history_private_elect_new_biggest (GPasteHistory *self)
{
    g_paste_history_private_elect_new_biggest_sparing (self, NULL);
}

/* Drop the item at @index from the model. The array's ref is *stolen* rather
 * than released, so this never frees on its own: with @departs the item leaves
 * the history for good and is disposed of here, without it the caller has
 * taken the ref over — either to free it itself or because the item is only
 * being moved to the front.
 *
 * Nothing the storage backend wrote for the item goes with it: an image's file
 * goes with the next full write that no longer references it, the file
 * flavours' sweep (gpaste-file-backend.c says why there). */
static void
g_paste_history_private_remove (GPasteHistory *self,
                                guint          index,
                                gboolean       departs)
{
    g_return_if_fail (index < self->history->len);

    GPasteItem *item = g_ptr_array_index (self->history, index);

    self->size -= g_paste_item_get_size (item);
    if (G_PASTE_IS_PASSWORD_ITEM (item))
        --self->passwords;

    /* Before the steal: the key belongs to the item. */
    g_hash_table_remove (self->by_uuid, g_paste_item_get_uuid (item));
    g_ptr_array_steal_index (self->history, index);

    if (departs)
    {
        /* A password leaving the model for good may still be sitting on a
         * selection, and the entry that recorded it is about to be gone: nothing
         * would then take it off, and nothing would record it either. Announced
         * from here so that every way out is covered at once -- a delete, an
         * emptying, either cap evicting one -- whatever position it held, since a
         * selection carries what it carries and not what the history's head says
         * (GPasteHistory::password-dropped, and the /clipboard/dropped_password
         * tests). Which selections that is, if any, is the clipboards manager's
         * to answer.
         *
         * A @departs of %FALSE is an item being moved to the front or a
         * duplicate the caller frees, which is no departure: the value stays in
         * the history. */
        if (G_PASTE_IS_PASSWORD_ITEM (item))
            self->pending_dropped = g_slist_prepend (self->pending_dropped, g_object_ref (item));

        g_object_unref (item);
    }
}

static void
g_paste_history_selected (GPasteHistory *self,
                          GPasteItem    *item)
{
    /* Emitting now would drive the clipboards manager, hence GTK's clipboard
     * and the X11 selection machinery, with our lock held; handlers may also
     * call back into us (g_paste_history_remove when an item turns out to be
     * invalid), which would deadlock on our non-recursive lock. Just record
     * the item: G_PASTE_LOCK_HISTORY emits it once the lock is released.
     */
    g_set_object (&self->pending_selection, item);
    self->pending_clear = !item;
}

static void
g_paste_history_emit_switch (GPasteHistory *self,
                             const gchar   *name)
{
    g_debug ("history: switch");

    g_signal_emit (self,
                   signals[SWITCH],
                   0, /* detail */
                   name,
                   NULL);
}

/* A ref-holding copy of the current history, to be handed to the saver.
 *
 * GList is the storage layer's interchange type and stays that way: writing a
 * history out is a one-shot bulk operation where a list costs nothing, so the
 * backends, the saver and the migration code keep taking one. Only the live
 * model is an array. Built back to front, so the list comes out newest-first
 * like the array it is copied from. */
static GList *
g_paste_history_snapshot (GPasteHistory *self)
{
    GList *snapshot = NULL;

    for (guint i = self->history->len; i > 0; --i)
        snapshot = g_list_prepend (snapshot, g_object_ref (g_ptr_array_index (self->history, i - 1)));

    return snapshot;
}

/* Drop every item, keeping the containers. Releases the array's refs through
 * its free func (a plain unref: unlike g_paste_history_empty this is a history
 * being swapped out, not thrown away -- what the reload of one leaves out being
 * reload_backend ()'s to announce). */
static void
g_paste_history_private_clear (GPasteHistory *self)
{
    g_ptr_array_set_size (self->history, 0);
    g_hash_table_remove_all (self->by_uuid);
    self->passwords = 0;
}

/* Take over an item list from the storage layer (transfer full) and install it
 * as the model, rebuilding the uuid index with it. */
static void
g_paste_history_private_set_from_list (GPasteHistory *self,
                                       GList         *history)
{
    g_paste_history_private_clear (self);

    for (GList *h = history; h; h = g_list_next (h))
    {
        GPasteItem *item = h->data;

        g_ptr_array_add (self->history, item);
        g_hash_table_insert (self->by_uuid, (gpointer) g_paste_item_get_uuid (item), item);
        if (G_PASTE_IS_PASSWORD_ITEM (item))
            ++self->passwords;
    }

    /* The refs moved into the array; only the links are ours to free. */
    g_list_free (history);
}

static void
g_paste_history_emit_update (GPasteHistory     *self,
                             GPasteUpdateAction action,
                             GPasteUpdateTarget target,
                             const gchar       *uuid,
                             guint64            position)
{
    g_debug ("history: update");

    g_signal_emit (self,
                   signals[UPDATE],
                   0, /* detail */
                   action,
                   target,
                   (uuid) ?: "",
                   position,
                   NULL);
}

/* How many of the model's items its store holds once they are written, which
 * is what a history just left is counted as until they are when the write
 * still out carries no snapshot (g_paste_history_saver_count_pending ()): every
 * one on an encrypted flavour, all but the passwords otherwise, as
 * g_paste_storage_backend_stores_item () rules. The passwords are counted as
 * the model changes, every recorded change asking this, rather than walked
 * each time. */
static guint64
g_paste_history_stored_length (GPasteHistory *self)
{
    return self->history->len - ((g_paste_storage_backend_is_encrypted (self->backend)) ? 0 : self->passwords);
}

/* Have the saver persist a change to the history. @displaced says whether
 * anything left the history alongside it: a dedup, a grown line replacing its
 * shorter self, or an eviction. It only means anything for an add on an
 * incremental backend -- see below. */
static void
g_paste_history_record (GPasteHistory      *self,
                        GPasteHistorySaveOp op,
                        GPasteItem         *item,
                        const gchar        *uuid,
                        gboolean            displaced)
{
    /* Don't persist intermediate states while an async load is replacing the
     * history (the load result triggers its own save when appropriate), nor once
     * we have been flushed for handover (a successor daemon owns the file now).
     * Nor a history nobody has named, which has no store to persist into: the
     * saver files every write under the history's name. */
    if (!self->name || self->stopped || self->unreadable || g_paste_history_saver_is_loading (self->saver))
        return;

    /* A non-incremental backend rewrites everything and always needs the
     * snapshot. An incremental one only reconciles what rode along with an
     * add -- and reconciliation only ever deletes rows the history no longer
     * has, so with nothing displaced there is provably nothing to delete.
     * That is the common case, and it is what spares a per-item ref-copy of
     * the whole history on every single clipboard change. A store a failed
     * write left behind is the saver's to report
     * (g_paste_history_on_diverged ()). */
    gboolean full = !g_paste_storage_backend_is_incremental (self->backend);
    GList *snapshot = (full || op == G_PASTE_HISTORY_SAVE_FULL || (op == G_PASTE_HISTORY_SAVE_ADD && displaced))
        ? g_paste_history_snapshot (self)
        : NULL;

    g_paste_history_saver_record (self->saver, op, self->name, item, uuid, snapshot, g_paste_history_stored_length (self));
}

/* A write the saver had out left the store of @name behind the model: see
 * #GPasteHistorySaverDivergedFunc. A history switched away from has no model
 * left to write, and stays behind until a write of it lands. */
static void
g_paste_history_on_diverged (gpointer     user_data,
                             const gchar *name)
{
    GPasteHistory *self = user_data;

    if (!g_paste_str_equal (name, self->name))
        return;

    G_PASTE_DO_LOCK_HISTORY;

    g_paste_history_record (self, G_PASTE_HISTORY_SAVE_FULL, NULL, NULL, FALSE);
}

/* Record the change (@displaced is g_paste_history_record ()'s), and announce
 * it. */
static void
g_paste_history_update (GPasteHistory      *self,
                        GPasteUpdateAction  action,
                        GPasteUpdateTarget  target,
                        guint64             position,
                        GPasteHistorySaveOp op,
                        GPasteItem         *item,
                        const gchar        *uuid,
                        gboolean            displaced)
{
    g_paste_history_record (self, op, item, uuid, displaced);

    /* The item the change is about: whichever one it left behind, or the uuid
     * alone when it left none (a removal). Neither rides along with an ALL,
     * which is about the history and not about any one item of it: the
     * interface says both are empty there, and a client that reads the uuid
     * whenever it is non-empty would otherwise act on an item the update was
     * never about. The saver still gets @item -- what it persists and what the
     * signal is about are two different questions. */
    if (target == G_PASTE_UPDATE_TARGET_ALL)
        g_paste_history_emit_update (self, action, target, NULL, 0);
    else
        g_paste_history_emit_update (self, action, target, (item) ? g_paste_item_get_uuid (item) : uuid, position);
}

static void
g_paste_history_activate_first (GPasteHistory *self,
                                gboolean       select)
{
    if (!self->history->len)
        return;

    GPasteItem *first = g_ptr_array_index (self->history, 0);

    self->size -= g_paste_item_get_size (first);
    g_paste_item_set_state (first, G_PASTE_ITEM_STATE_ACTIVE);
    self->size += g_paste_item_get_size (first);

    /* Becoming the head through load, removal or a refused-item fallback is
     * not an explicit request to expose a password. select() emits separately. */
    if (select && !G_PASTE_IS_PASSWORD_ITEM (first))
        g_paste_history_selected (self, first);
}

static GPasteItem *
g_paste_history_private_get_by_uuid (GPasteHistory *self,
                                     const gchar   *uuid)
{
    return (uuid) ? g_hash_table_lookup (self->by_uuid, uuid) : NULL;
}

/* The item plus where it currently sits.
 *
 * The position is looked up rather than indexed, because indexing it would cost
 * more than it saves: adding prepends, which shifts every position, so a
 * uuid->position map would have to be rewritten on every single add. The scan
 * compares pointers, the item having already been found by uuid. */
static GPasteItem *
g_paste_history_private_get_indexed_by_uuid (GPasteHistory *self,
                                             const gchar   *uuid,
                                             guint         *index)
{
    GPasteItem *item = g_paste_history_private_get_by_uuid (self, uuid);

    if (!item)
        return NULL;

    /* In the index but not in the array would mean the two had drifted apart.
     * Checked with a real branch rather than g_return_val_if_fail(): that one
     * compiles away entirely under G_DISABLE_CHECKS, taking the lookup with it
     * and handing the caller an @index its stack happened to hold. */
    if (!g_ptr_array_find (self->history, item, index))
    {
        g_critical ("The item with uuid \"%s\" is indexed but not in the history", uuid);
        return NULL;
    }

    return item;
}

/* @spared is left out of every election, as in
 * g_paste_history_private_elect_new_biggest_sparing (). */
static void
g_paste_history_private_check_memory_usage_sparing (GPasteHistory *self,
                                                    GPasteItem    *spared)
{
    guint64 max_memory = g_paste_settings_get_max_memory_usage (self->settings) * 1024 * 1024;

    /* A head that alone weighs more than the cap -- an image selected after the
     * cap was lowered under its decoded size -- no eviction brings under it:
     * emptying the history around it would lose every other entry and still not
     * fit, which _g_paste_history_replace () refuses an edit over. Once the head
     * goes idle, the history is weighed again. */
    if (self->history->len && g_paste_item_get_size (g_ptr_array_index (self->history, 0)) > max_memory)
        return;

    while (self->size > max_memory && self->biggest_uuid)
    {
        guint index;

        if (g_paste_history_private_get_indexed_by_uuid (self, self->biggest_uuid, &index))
            g_paste_history_private_remove (self, index, TRUE);

        /* Also re-points biggest_uuid, which borrowed its string from the item
         * just freed. */
        g_paste_history_private_elect_new_biggest_sparing (self, spared);
    }
}

static void
g_paste_history_private_check_memory_usage (GPasteHistory *self)
{
    g_paste_history_private_check_memory_usage_sparing (self, NULL);
}

static void
g_paste_history_private_check_size (GPasteHistory *self)
{
    guint64 max_history_size = g_paste_settings_get_max_history_size (self->settings);

    /* Truncate from the tail: private_remove already does the size bookkeeping,
     * the uuid index and the passwords that leave.
     *
     * Favourites are skipped rather than dropped, so this walks the tail once
     * instead of always taking the last item. A history holding nothing else
     * therefore grows past max_history_size, which is the whole point of
     * pinning: the cap gives way, not the item.
     *
     * Down to 1, never 0: the first item is the one just added or selected, and
     * a history whose every other entry is pinned would otherwise evict it on
     * the spot -- the clipboard would stop keeping anything at all. The memory
     * cap leaves that slot alone for the same reason. */
    gboolean dropped_biggest = FALSE;

    for (guint i = self->history->len; --i > 0 && self->history->len > max_history_size; )
    {
        GPasteItem *item = g_ptr_array_index (self->history, i);

        if (g_paste_item_is_favourite (item))
            continue;

        /* Dropped the moment the elected item is the one going, not once the
         * loop is over: biggest_uuid borrows that item's own string, and the
         * removal below frees it, so every later iteration would be comparing
         * against freed memory. */
        if (g_paste_str_equal (self->biggest_uuid, g_paste_item_get_uuid (item)))
        {
            self->biggest_uuid = NULL;
            self->biggest_size = 0;
            dropped_biggest = TRUE;
        }

        g_paste_history_private_remove (self, i, TRUE);
    }

    /* Left to the end: the runner-up can only be named once the loop has taken
     * everything it is going to take. The memory cap re-elects after each of its
     * own evictions instead, because each one decides what it evicts next. */
    if (dropped_biggest)
        g_paste_history_private_elect_new_biggest (self);
}

/* Both caps, applied when a cap itself moves rather than the history under it:
 * the same trim g_paste_history_on_loaded () performs, saved and announced, and
 * left alone on a flushed or unreadable history, for the reasons it gives. */
static void
g_paste_history_private_trim (GPasteHistory *self)
{
    if (self->stopped || self->unreadable)
        return;

    guint length_before = self->history->len;

    g_paste_history_private_check_size (self);
    g_paste_history_private_check_memory_usage (self);

    if (self->history->len != length_before)
        g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0, G_PASTE_HISTORY_SAVE_FULL, NULL, NULL, FALSE);
}

/* A line grows as it is typed or selected, which only plain text and uri lists
 * do. An image has no meaningful prefix, and a password is never merged into
 * its neighbour. */
static gboolean
g_paste_history_private_kind_grows (GPasteItemKind kind)
{
    return kind == G_PASTE_ITEM_KIND_TEXT || kind == G_PASTE_ITEM_KIND_URIS;
}

static gboolean
g_paste_history_private_is_growing_line (GPasteHistory *self,
                                         GPasteItem    *old,
                                         GPasteItem    *new)
{
    if (!g_paste_settings_get_growing_lines (self->settings) ||
        !g_paste_history_private_kind_grows (g_paste_item_get_kind (old)) ||
        !g_paste_history_private_kind_grows (g_paste_item_get_kind (new)))
        return FALSE;

    const gchar *n = g_paste_item_get_value (new);
    const gchar *o = g_paste_item_get_value (old);

    return (g_str_has_prefix (n, o) || g_str_has_suffix (n, o));
}

/* What the arriving duplicate carries beyond the value it matched the head on,
 * moved onto the head that is kept in its place.
 *
 * The dedup deeper in the history puts the arriving item in the matched entry's
 * place, so whatever it carries comes with it; the head is the one case where
 * the older object stays and the newer one goes, and without this the same copy
 * would be answered two ways depending on where its twin happened to sit.
 *
 * For a password that is its timeout: two nameless ones match on their value
 * alone, and the one just read off a selection was built with whatever
 * password-timeout says now -- which is the duration a later selection of this
 * entry has to run for. Written in place and recorded like any other such write
 * (see g_paste_history_make_password ()), the add stopping here being what would
 * otherwise leave the store carrying the duration that has just been replaced. */
static void
g_paste_history_private_merge_into_head (GPasteHistory *self,
                                         GPasteItem    *head,
                                         GPasteItem    *duplicate)
{
    /* On select the two are one object, which carries nothing to move. */
    if (head == duplicate || !G_PASTE_IS_PASSWORD_ITEM (head) || !G_PASTE_IS_PASSWORD_ITEM (duplicate))
        return;

    guint timeout = g_paste_password_item_get_timeout (G_PASTE_PASSWORD_ITEM (duplicate));

    if (timeout == g_paste_password_item_get_timeout (G_PASTE_PASSWORD_ITEM (head)))
        return;

    g_paste_password_item_set_timeout (G_PASTE_PASSWORD_ITEM (head), timeout);
    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ITEM, 0, G_PASTE_HISTORY_SAVE_REPLACE, head, g_paste_item_get_uuid (head), FALSE);
}

static void
_g_paste_history_add (GPasteHistory *self,
                      GPasteItem    *item,
                      gboolean       new_selection)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));
    g_return_if_fail (G_PASTE_IS_ITEM (item));

    guint64 max_memory = g_paste_settings_get_max_memory_usage (self->settings) * 1024 * 1024;

    /* On add (new_selection) we own @item (transfer full) and must consume it on
     * every path; on select it is borrowed (already in the history) and merely
     * moved. Holding it here frees it automatically on the early-return paths;
     * it is g_steal_pointer'd into the list once it is actually stored. */
    g_autoptr (GPasteItem) owned = new_selection ? item : NULL;

    /* Refresh and password retirement pass an extra owned reference to an item
     * already in the history. Transfer-full applies to the reference, not to
     * object uniqueness: move the identity without emitting "selected", and
     * consume the extra reference above. A growing-line match must not insert
     * this same object twice. */
    if (new_selection && g_paste_history_private_get_by_uuid (self, g_paste_item_get_uuid (item)) == item)
    {
        _g_paste_history_add (self, item, FALSE);
        return;
    }

    /* Only an add is refused. What a select moves is already in the history,
     * where the cap lets it stay as the head or a favourite, and a select left
     * where it is would announce an item the head does not hold. */
    if (new_selection && g_paste_item_get_size (item) > max_memory)
        return;

    guint length_before = self->history->len;
    gboolean election_needed = !length_before; // If we don't have an history we want to initalize the biggest

    g_debug ("history: add");

    if (self->history->len)
    {
        GPasteItem *old_first = g_ptr_array_index (self->history, 0);
        /* On select, @item is already in the history and it is that very object
         * that moves, so what the head is asked is whether it *is* @item, never
         * whether it looks like it: two nameless passwords holding the same
         * value are equal, and selecting the deeper one would otherwise leave it
         * where it sat while the head answered for it. On add there is no
         * identity to ask about -- @item is a new object -- and the likeness is
         * the whole point. */
        gboolean head_matches = (new_selection) ? g_paste_item_equals (old_first, item) : (old_first == item);

        if (head_matches)
        {
            g_paste_history_private_merge_into_head (self, old_first, item);

            return;
        }

        if (new_selection && g_paste_history_private_is_growing_line (self, old_first, item))
        {
            if (g_paste_str_equal (self->biggest_uuid, g_paste_item_get_uuid (old_first)))
                election_needed = TRUE;
            /* The grown line is the same line the user pinned, only longer, so
             * it inherits the pin: dropping it here would quietly make what a
             * user asked to keep evictable again, one keystroke at a time. */
            if (g_paste_item_is_favourite (old_first))
                g_paste_item_set_favourite (item, TRUE);
            /* No departure: the grown item carries the same line on. */
            g_autoptr (GPasteItem) dropped = old_first;
            g_paste_history_private_remove (self, 0, FALSE);
        }
        else
        {
            /* Going idle changes what the item weighs, so it is taken out at
             * its old size and put back at its new one. */
            self->size -= g_paste_item_get_size (old_first);
            g_paste_item_set_state (old_first, G_PASTE_ITEM_STATE_IDLE);

            guint64 size = g_paste_item_get_size (old_first);

            self->size += size;

            /* It just stopped being the untracked active item, so it becomes a
             * candidate -- unless it is a favourite, which is never one. */
            if (size >= self->biggest_size && !g_paste_item_is_favourite (old_first))
            {
                self->biggest_uuid = g_paste_item_get_uuid (old_first);
                self->biggest_size = size;
            }

            /* Dedup is by value, not by uuid, so the index cannot help here --
             * and no more on select, where the uuid it does answer for leads
             * back to the object rather than to where it sits. */
            for (guint i = 1; i < self->history->len; ++i)
            {
                GPasteItem *entry = g_ptr_array_index (self->history, i);
                /* Identity on select, for the reason the head is asked the same
                 * way: the entry to move is the one that *is* @item, and a
                 * likeness deeper in the history is somebody else's row. */
                gboolean matches = (new_selection) ? (g_paste_item_equals (entry, item) ||
                                                      g_paste_history_private_is_growing_line (self, entry, item))
                                                   : (entry == item);

                if (matches)
                {
                    if (g_paste_str_equal (self->biggest_uuid, g_paste_item_get_uuid (entry)))
                        election_needed = TRUE;
                    /* Same as above: what arrives takes the place of a pinned
                     * entry, so it takes its pin with it. On select the two are
                     * one object and this says nothing new. */
                    if (g_paste_item_is_favourite (entry))
                        g_paste_item_set_favourite (item, TRUE);
                    /* On add, @entry is a distinct duplicate to free. On select,
                     * @entry IS @item being moved to the front, so its ref must
                     * be left alone. */
                    g_autoptr (GPasteItem) dropped = new_selection ? entry : NULL;
                    g_paste_history_private_remove (self, i, FALSE);
                    break;
                }
            }
        }
    }

    g_ptr_array_insert (self->history, 0, item);
    g_hash_table_insert (self->by_uuid, (gpointer) g_paste_item_get_uuid (item), item);
    if (G_PASTE_IS_PASSWORD_ITEM (item))
        ++self->passwords;
    g_steal_pointer (&owned); /* ownership transferred to the history */

    if (new_selection && self->stopped)
        g_hash_table_add (self->stopped_adds, g_strdup (g_paste_item_get_uuid (item)));

    /* Counted in at what it weighs now, and only then made active: activating
     * takes the head out at that weight and puts it back at its active one, so
     * an idle image being selected adds its texture once. */
    self->size += g_paste_item_get_size (item);
    g_paste_history_activate_first (self, FALSE);

    /* Before the caps, not after: whatever was displaced above has already been
     * freed, and biggest_uuid borrows its string from the item it names, so an
     * election left until later would leave check_size () comparing against
     * memory that is gone. */
    if (election_needed)
        g_paste_history_private_elect_new_biggest (self);

    g_paste_history_private_check_size (self);
    g_paste_history_private_check_memory_usage (self);

    /* One in and nothing out means nothing was deduped, grown over or evicted;
     * anything else and the store has rows the history no longer has. */
    gboolean displaced = self->history->len != length_before + 1;

    /* TARGET_ALL whichever path got here. An ordinary add shifts every position
     * along; a grown line is a new item, with a uuid of its own, standing where
     * the one it grew from stood -- and one uuid cannot say both which item went
     * and which arrived, the same reason _g_paste_history_replace () reports
     * ALL. A view keyed on the old one would go on showing it. */
    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0, G_PASTE_HISTORY_SAVE_ADD, item, NULL, displaced);
}

/**
 * g_paste_history_add:
 * @self: a #GPasteHistory instance
 * @item: (transfer full): the #GPasteItem to add
 *
 * Add a #GPasteItem to the #GPasteHistory
 */
G_PASTE_VISIBLE void
g_paste_history_add (GPasteHistory *self,
                     GPasteItem    *item)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));
    g_return_if_fail (G_PASTE_IS_ITEM (item));

    G_PASTE_LOCK_HISTORY;

    _g_paste_history_add (self, item, TRUE);
}

static void
g_paste_history_remove_common (GPasteHistory *self,
                               GPasteItem    *item,
                               guint          index)
{
    if (!item)
        return;

    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (item));
    gboolean was_biggest = g_paste_str_equal (self->biggest_uuid, uuid);

    g_paste_history_private_remove (self, index, TRUE);

    if (!index)
    {
        g_paste_history_activate_first (self, TRUE);

        /* What was removed may still be on the selections, and a password at
         * the head may not replace it (see activate_first ()): the selections
         * are cleared instead (the /clipboard/automatic_head tests). Ordinary
         * text is left where it is: it is the user's own copy, and deleting the
         * entry that listed it is not asking for it to be taken off.
         *
         * This is about the head that was not published and nothing else. What
         * left is private_remove ()'s to announce, and that is what reaches the
         * selections actually carrying it -- from here, from any other index, or
         * with no head left to publish at all. */
        if (self->history->len && G_PASTE_IS_PASSWORD_ITEM (g_ptr_array_index (self->history, 0)))
            g_paste_history_selected (self, NULL);
    }

    /* The head going puts the next item in its place, and the item that was
     * elected may well be that one: active now, it is not the cap's to evict.
     * Only then: one elected deeper is still the one to elect, the election
     * walking the whole history. */
    if (was_biggest || (!index && self->history->len &&
                        g_paste_str_equal (self->biggest_uuid, g_paste_item_get_uuid (g_ptr_array_index (self->history, 0)))))
        g_paste_history_private_elect_new_biggest (self);

    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REMOVE, G_PASTE_UPDATE_TARGET_ITEM, index, G_PASTE_HISTORY_SAVE_REMOVE, NULL, uuid, FALSE);
}

static void
g_paste_history_remove_locked (GPasteHistory *self,
                               guint64        index)
{
    g_debug ("history: remove '%" G_GUINT64_FORMAT "'", index);

    if (index >= self->history->len)
        return;

    g_paste_history_remove_common (self, g_ptr_array_index (self->history, index), (guint) index);
}

/**
 * g_paste_history_remove:
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteItem to delete
 *
 * Delete a #GPasteItem from the #GPasteHistory
 */
G_PASTE_VISIBLE void
g_paste_history_remove (GPasteHistory *self,
                        guint64        index)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));

    G_PASTE_LOCK_HISTORY;

    g_paste_history_remove_locked (self, index);
}

/**
 * g_paste_history_remove_by_uuid:
 * @self: a #GPasteHistory instance
 * @uuid: the uuid of the #GPasteItem to delete
 *
 * Delete a #GPasteItem from the #GPasteHistory
 *
 * Returns: whether we removed anything
 */
G_PASTE_VISIBLE gboolean
g_paste_history_remove_by_uuid (GPasteHistory *self,
                                const gchar   *uuid)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), FALSE);

    G_PASTE_LOCK_HISTORY;

    g_debug ("history: remove '%s", uuid);

    guint index;
    GPasteItem *item = g_paste_history_private_get_indexed_by_uuid (self, uuid, &index);

    if (!item)
        return FALSE;

    g_paste_history_remove_common (self, item, index);
    return TRUE;
}

static GPasteItem *
g_paste_history_private_get (GPasteHistory *self,
                             guint64        index)
{
    return (index < self->history->len) ? g_ptr_array_index (self->history, index) : NULL;
}

/**
 * g_paste_history_get:
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteItem
 *
 * Get a #GPasteItem from the #GPasteHistory
 *
 * Returns: (transfer none) (nullable): the #GPasteItem at @index, still owned by
 *          the history, or %NULL when @index is past its end
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_history_get (GPasteHistory *self,
                     guint64        index)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);

    G_PASTE_LOCK_HISTORY;

    return g_paste_history_private_get (self, index);
}

/**
 * g_paste_history_get_by_uuid:
 * @self: a #GPasteHistory instance
 * @uuid: the uuid of the #GPasteItem
 *
 * Get a #GPasteItem from the #GPasteHistory
 *
 * Returns: (transfer none) (nullable): the #GPasteItem carrying @uuid, still
 *          owned by the history, or %NULL when no item does
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_history_get_by_uuid (GPasteHistory *self,
                             const gchar   *uuid)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);

    G_PASTE_LOCK_HISTORY;

    return g_paste_history_private_get_by_uuid (self, uuid);
}

/**
 * g_paste_history_dup:
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteItem
 *
 * Get a #GPasteItem from the #GPasteHistory
 * free it with g_object_unref
 *
 * Returns: (transfer full) (nullable): a #GPasteItem, or %NULL when @index is
 *          out of range (an empty history has no item 0)
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_history_dup (GPasteHistory *self,
                     guint64        index)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);

    G_PASTE_LOCK_HISTORY;

    GPasteItem *item = g_paste_history_private_get (self, index);

    /* Callers null-check the result; ref'ing NULL would only add a critical. */
    return (item) ? g_object_ref (item) : NULL;
}

/**
 * g_paste_history_select:
 * @self: a #GPasteHistory instance
 * @uuid: the uuid of the #GPasteItem to select
 *
 * Select a #GPasteItem from the #GPasteHistory
 *
 * Returns: whether the item could be selected
 */
G_PASTE_VISIBLE gboolean
g_paste_history_select (GPasteHistory *self,
                        const gchar   *uuid)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), FALSE);
    g_debug ("history: select '%s'", uuid);

    G_PASTE_LOCK_HISTORY;
    GPasteItem *item = g_paste_history_private_get_by_uuid (self, uuid);

    if (!item)
        return FALSE;

    _g_paste_history_add (self, item, FALSE);
    g_paste_history_selected (self, item);
    return TRUE;
}

static void
_g_paste_history_replace (GPasteHistory *self,
                          guint          index,
                          GPasteItem    *new)
{
    GPasteItem *old = g_ptr_array_index (self->history, index);
    g_autofree gchar *old_uuid = g_strdup (g_paste_item_get_uuid (old));
    guint length_before = self->history->len;

    /* The pin belongs to the entry, not to the object holding its contents:
     * editing a pinned item, or turning one into a password, changes what it
     * says and nothing about the user having asked to keep it. Carried over
     * before the election below, which is the one thing the flag steers. */
    g_paste_item_set_favourite (new, g_paste_item_is_favourite (old));

    self->size -= g_paste_item_get_size (old);
    self->size += g_paste_item_get_size (new);

    /* Swap in place: the steal hands back the array's ref (its free func does
     * not run) so we drop it ourselves, and @new takes the same slot. */
    g_hash_table_remove (self->by_uuid, old_uuid);
    g_ptr_array_steal_index (self->history, index);
    if (G_PASTE_IS_PASSWORD_ITEM (old))
        --self->passwords;
    if (G_PASTE_IS_PASSWORD_ITEM (new))
        ++self->passwords;
    g_object_unref (old);
    g_ptr_array_insert (self->history, index, new);
    g_hash_table_insert (self->by_uuid, (gpointer) g_paste_item_get_uuid (new), new);

    /* The replacement can weigh more than what it replaced, so the memory cap
     * runs now: left to the next copy, the eviction this one calls for would
     * be blamed on that instead. It leaves the replacement alone this once, as
     * an add leaves the item it adds, and is back in the running right after:
     * evicting it would answer the edit with its loss, the original gone with
     * it, under a uuid the caller has just been told about, while a tracker
     * left without it would have the next cap evict something smaller first.
     * Replace refuses contents weighing more than the cap on their own before
     * getting here (g_paste_history_replace ()), and MakePassword a password
     * its name makes that heavy (g_paste_history_make_password ()). The first
     * election runs either way: the replaced item may have been the elected
     * one. */
    g_paste_history_private_elect_new_biggest_sparing (self, new);
    g_paste_history_private_check_memory_usage_sparing (self, new);
    g_paste_history_private_elect_new_biggest (self);

    if (self->history->len != length_before)
    {
        /* As in g_paste_history_set_favourite (): evictions rode along, and a
         * replace carries no snapshot for an incremental backend to reconcile
         * against, so the history is rewritten outright. What moved is every
         * position below the evictions, which only ALL says. */
        g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0, G_PASTE_HISTORY_SAVE_FULL, NULL, NULL, FALSE);
        return;
    }

    /* A replacement that keeps the uuid can update one row. With a new uuid,
     * one identifier cannot say both which item went and which arrived. */
    GPasteUpdateTarget target = g_paste_str_equal (old_uuid, g_paste_item_get_uuid (new))
        ? G_PASTE_UPDATE_TARGET_ITEM : G_PASTE_UPDATE_TARGET_ALL;

    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, target, index, G_PASTE_HISTORY_SAVE_REPLACE, new, old_uuid, FALSE);
}

/**
 * g_paste_history_replace:
 * @self: a #GPasteHistory instance
 * @uuid: the uuid of the #GPasteTextItem to replace
 * @contents: the new contents
 *
 * Replace the contents of the text item @uuid names
 *
 * The replacement is an item of its own, so it has a uuid of its own: that is
 * what comes back, since the update this raises names no item (see
 * _g_paste_history_replace()) and a caller would otherwise have no way to reach
 * what it just created.
 *
 * Contents weighing more than "max-memory-usage" on their own are refused, the
 * item being left as it was, as an add of them would be refused.
 *
 * Returns: (transfer full) (nullable): the new item's uuid, or %NULL when @uuid
 *          matched nothing or the contents were refused
 */
G_PASTE_VISIBLE gchar *
g_paste_history_replace (GPasteHistory *self,
                         const gchar   *uuid,
                         const gchar   *contents)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);
    g_return_val_if_fail (!contents || g_utf8_validate (contents, -1, NULL), NULL);

    G_PASTE_LOCK_HISTORY;
    guint index;
    GPasteItem *item = g_paste_history_private_get_indexed_by_uuid (self, uuid, &index);

    if (!item)
        return NULL;

    g_return_val_if_fail (G_PASTE_IS_TEXT_ITEM (item), NULL);

    g_autoptr (GPasteItem) new = g_paste_text_item_new (contents);

    /* Refused as an add of the same contents is (_g_paste_history_add ()):
     * kept, it would evict everything else and still not fit. */
    if (g_paste_item_get_size (new) > g_paste_settings_get_max_memory_usage (self->settings) * 1024 * 1024)
        return NULL;

    _g_paste_history_replace (self, index, g_object_ref (new));

    if (!index)
        g_paste_history_selected (self, new);

    return g_strdup (g_paste_item_get_uuid (new));
}

/**
 * g_paste_history_strip_rich_text:
 * @self: a #GPasteHistory instance
 * @uuid: the uuid of the #GPasteTextItem to strip
 * @found: (out) (optional): whether @uuid matched an item at all, which is what
 *         tells the two refusals apart -- no such item, or one of another kind
 *
 * Drop the rich text flavours of the item @uuid names, keeping the plain text
 * it is shown and pasted as
 *
 * The item stays where it is and keeps its uuid: what a rich copy carries
 * beyond its text is a set of alternative representations of that same text,
 * so dropping them changes nothing a listing shows. That is why the update this
 * raises names the item, unlike g_paste_history_replace()'s.
 *
 * The caller refreshes selections still carrying this text through
 * g_paste_clipboards_manager_refresh_text(). The history's position alone says
 * nothing about what a selection currently holds.
 *
 * Editing a text item and turning one into a password each drop the same thing
 * without being asked to, both of them minting an item that never had any.
 *
 * Only a #GPasteTextItem may be stripped: what a uris item keeps beside its text
 * is not another spelling of it but the payload a file manager pastes, so taking
 * it away would leave that item unpastable rather than plainer.
 *
 * Returns: %FALSE when @uuid matches no #GPasteTextItem, %TRUE otherwise -- an
 *          item that carried no rich text is stripped successfully, and nothing
 *          is saved
 */
G_PASTE_VISIBLE gboolean
g_paste_history_strip_rich_text (GPasteHistory *self,
                                 const gchar   *uuid,
                                 gboolean      *found)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), FALSE);

    if (found)
        *found = FALSE;

    G_PASTE_LOCK_HISTORY;
    guint index;
    GPasteItem *item = g_paste_history_private_get_indexed_by_uuid (self, uuid, &index);

    if (!item)
        return FALSE;

    if (found)
        *found = TRUE;

    /* Answered rather than asserted, the caller having asked the same question
     * already: it asked it of what its own unlocked lookup found, and this one
     * runs under the lock, so the two need not be looking at the same item. A
     * race lost is not a programmer error, and a g_critical () in the daemon's
     * log is not what it deserves. */
    if (!G_PASTE_IS_TEXT_ITEM (item))
        return FALSE;

    if (!g_paste_item_get_special_values (item))
        return TRUE;

    /* Save snapshots hold references to live items, and their worker may be
     * iterating the rich representations. Keep that object intact for those
     * readers; the plain replacement preserves the entry's identity and pin. */
    GPasteItem *plain = g_paste_text_item_new (g_paste_item_get_real_value (item));

    g_paste_item_set_uuid (plain, g_paste_item_get_uuid (item));
    _g_paste_history_replace (self, index, plain);

    return TRUE;
}

/**
 * g_paste_history_set_favourite:
 * @self: a #GPasteHistory instance
 * @uuid: the uuid of the #GPasteItem to pin, or to let go of
 * @favourite: whether the item should be pinned
 *
 * Pin an item, exempting it from the size and memory caps, or let it go again.
 *
 * Un-pinning puts the item back under both caps at once, so it may well be the
 * very call that evicts it — this returns %TRUE all the same: the item is gone
 * because the caller asked for it to be evictable.
 *
 * Returns: %FALSE when @uuid matches no item, %TRUE otherwise
 */
G_PASTE_VISIBLE gboolean
g_paste_history_set_favourite (GPasteHistory *self,
                               const gchar   *uuid,
                               gboolean       favourite)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), FALSE);

    G_PASTE_LOCK_HISTORY;
    guint index;
    GPasteItem *item = g_paste_history_private_get_indexed_by_uuid (self, uuid, &index);

    if (!item)
        return FALSE;

    if (g_paste_item_is_favourite (item) == favourite)
        return TRUE;

    g_debug ("history: set favourite '%s'", uuid);

    /* Our own copy: un-pinning can evict the very item we are holding. */
    g_autofree gchar *item_uuid = g_strdup (uuid);
    guint length_before = self->history->len;

    g_paste_item_set_favourite (item, favourite);

    if (favourite)
    {
        /* The pool of eviction candidates lost this item. Only a full election
         * can name the runner-up, and only when it was the one elected. */
        if (g_paste_str_equal (self->biggest_uuid, item_uuid))
            g_paste_history_private_elect_new_biggest (self);
    }
    else
    {
        /* The pool gained it, so nothing has to be re-elected: one comparison
         * places it, the same way _g_paste_history_add places an item leaving
         * the active slot. Index 0 stays untracked, as ever. */
        guint64 size = g_paste_item_get_size (item);

        if (index && size >= self->biggest_size)
        {
            self->biggest_uuid = g_paste_item_get_uuid (item);
            self->biggest_size = size;
        }

        /* Both caps may have been giving way to this item for a while. */
        g_paste_history_private_check_size (self);
        g_paste_history_private_check_memory_usage (self);
    }

    if (self->history->len != length_before)
    {
        /* Evictions rode along. A replace carries no snapshot for an incremental
         * backend to reconcile against — unlike an add, which is the one
         * operation that expects to displace anything — so rewrite the history
         * outright rather than leave those rows behind. Rare: it takes a history
         * this item was holding over one of its caps. @item may be among the
         * evicted, hence neither it nor @index is touched from here on. */
        g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0, G_PASTE_HISTORY_SAVE_FULL, NULL, NULL, FALSE);
    }
    else
        g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ITEM, index, G_PASTE_HISTORY_SAVE_REPLACE, item, item_uuid, FALSE);

    return TRUE;
}

static GPasteItem *
_g_paste_history_private_get_password (GPasteHistory *self,
                                       const gchar   *name,
                                       guint64       *index)
{
    for (guint idx = 0; idx < self->history->len; ++idx)
    {
        GPasteItem *i = g_ptr_array_index (self->history, idx);

        if (G_PASTE_IS_PASSWORD_ITEM (i) &&
            g_paste_str_equal (g_paste_password_item_get_name ((GPastePasswordItem *) i), name))
        {
            if (index)
                *index = idx;
            return i;
        }
    }

    if (index)
        *index = -1;
    return NULL;
}

/**
 * g_paste_history_make_password:
 * @self: a #GPasteHistory instance
 * @uuid: the uuid of the #GPasteTextItem to turn into a password, or of the
 *        #GPastePasswordItem to update
 * @name: (nullable): the name to give to the password, or %NULL to leave a
 *        password's own name alone -- a text item becoming one takes
 *        %G_PASTE_PASSWORD_ITEM_NO_NAME
 * @timeout: how long it may stay on the clipboard, in seconds, 0 for as long as
 *           anything else, or %G_PASTE_PASSWORD_TIMEOUT_UNCHANGED to write none
 *           -- a password keeping the one it carries, a text item becoming one
 *           taking the "password-timeout" default
 *
 * Mark a text item as password, or write a password's name and timeout
 *
 * The two hand back different uuids. A text item is *replaced*: a password item
 * is an item of its own, with a uuid of its own, for the same reason
 * g_paste_history_replace() hands one back. A password item keeps its place and
 * its uuid -- a password carrying the new name and timeout, never another
 * value, takes its place under it -- so what comes back is the uuid that went
 * in.
 *
 * Returns: (transfer full) (nullable): the uuid of the item this left in the
 *          history, or %NULL with nothing changed when @name is another
 *          password's, or when the password, its name included, would weigh
 *          more than max-memory-usage
 */
G_PASTE_VISIBLE gchar *
g_paste_history_make_password (GPasteHistory *self,
                               const gchar   *uuid,
                               const gchar   *name,
                               guint          timeout)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);
    g_return_val_if_fail (!name || g_utf8_validate (name, -1, NULL), NULL);

    G_PASTE_LOCK_HISTORY;
    guint index;
    GPasteItem *item = g_paste_history_private_get_indexed_by_uuid (self, uuid, &index);

    g_return_val_if_fail (item, NULL);
    g_return_val_if_fail (G_PASTE_IS_TEXT_ITEM (item) || G_PASTE_IS_PASSWORD_ITEM (item), NULL);

    /* Keeping the name it already carries is not a collision with itself: only
     * the timeout may be what the caller came to change. Nor is a nameless one,
     * since they all go by G_PASTE_PASSWORD_ITEM_NO_NAME: that is no name a user
     * picked, so it is the one a history may hold several of. */
    gboolean named = (name && !g_paste_str_equal (name, G_PASTE_PASSWORD_ITEM_NO_NAME));
    GPasteItem *namesake = (named) ? _g_paste_history_private_get_password (self, name, NULL) : NULL;

    /* Not a g_return_val_if_fail (): a name the user picked twice is an ordinary
     * refusal and not a programming error, so it neither warrants a CRITICAL nor
     * may compile away under G_DISABLE_CHECKS -- g_paste_daemon_methods_make_password ()
     * reports it as .Error.AlreadyExists before ever reaching here, and a caller
     * coming straight to the library gets the same rule rather than a second
     * password under a name already taken. */
    if (namesake && namesake != item)
        return NULL;

    /* Nothing to write means whatever it already has: what a password carries,
     * and for a text item -- which carries none -- the default the settings hold
     * for a password being made. */
    if (timeout == G_PASTE_PASSWORD_TIMEOUT_UNCHANGED)
    {
        timeout = (G_PASTE_IS_PASSWORD_ITEM (item))
                ? g_paste_password_item_get_timeout (G_PASTE_PASSWORD_ITEM (item))
                : (guint) g_paste_settings_get_password_timeout (self->settings);
    }

    /* A password is never written in place, a new one taking its place under
     * its uuid instead: the saver's snapshots hold the live items, and a worker
     * may be writing this one's name out while the main thread would free it
     * (/history-ordering/rename-during-write). The pin, the memory total and
     * the cap follow through _g_paste_history_replace (), as for a text item
     * becoming a password. */
    const gchar *keep = (!name && G_PASTE_IS_PASSWORD_ITEM (item)) ? g_paste_password_item_get_name (G_PASTE_PASSWORD_ITEM (item)) : name;
    g_autoptr (GPasteItem) password = g_paste_password_item_new (keep, g_paste_item_get_real_value (item), timeout);

    if (G_PASTE_IS_PASSWORD_ITEM (item))
        g_paste_item_set_uuid (password, g_paste_item_get_uuid (item));

    /* Its name weighs in, twice over (the display string carries it too), and
     * nothing else bounds it: the replacement is spared by the cap, which
     * would evict everything else to make room for it, as Replace refuses
     * contents that heavy (g_paste_history_replace ()). */
    if (g_paste_item_get_size (password) > g_paste_settings_get_max_memory_usage (self->settings) * 1024 * 1024)
        return NULL;

    _g_paste_history_replace (self, index, g_object_ref (password));

    /* Nothing is selected, wherever the item sits: a password is published as
     * g_paste_item_get_real_value (), which this never changes, and the item
     * at the front is not necessarily what a selection holds -- selecting would
     * be writing its cleartext over whatever the user has copied since. Arming
     * the countdown the new timeout calls for is
     * g_paste_clipboards_manager_rearm_password ()'s job, the clipboards being
     * what the history has no reach into. */
    return g_strdup (g_paste_item_get_uuid (password));
}

/**
 * g_paste_history_get_password:
 * @self: a #GPasteHistory instance
 * @name: the name of the #GPastePasswordItem
 *
 * Get the first password matching name
 *
 * Returns: (transfer none) (nullable): the #GPastePasswordItem, still owned by
 *          the history, or %NULL when no password carries @name
 */
G_PASTE_VISIBLE GPastePasswordItem *
g_paste_history_get_password (GPasteHistory *self,
                              const gchar   *name)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);
    g_return_val_if_fail (!name || g_utf8_validate (name, -1, NULL), NULL);

    G_PASTE_LOCK_HISTORY;
    GPasteItem *item = _g_paste_history_private_get_password (self, name, NULL);

    return (item) ? G_PASTE_PASSWORD_ITEM (item) : NULL;
}

/**
 * g_paste_history_delete_password:
 * @self: a #GPasteHistory instance
 * @name: the name of the #GPastePasswordItem
 *
 * Delete the password matching name
 *
 * Returns: %FALSE when no password carries @name, %TRUE otherwise
 */
G_PASTE_VISIBLE gboolean
g_paste_history_delete_password (GPasteHistory *self,
                                 const gchar   *name)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), FALSE);
    g_return_val_if_fail (!name || g_utf8_validate (name, -1, NULL), FALSE);

    G_PASTE_LOCK_HISTORY;
    guint64 index;

    if (!_g_paste_history_private_get_password (self, name, &index))
        return FALSE;

    g_paste_history_remove_locked (self, index);

    return TRUE;
}

/* Empty the model, recording the emptying as any change is: see
 * g_paste_history_update () for when that is. */
static void
g_paste_history_empty_locked (GPasteHistory *self)
{
    /* Through private_remove rather than the array's own free func: emptying
     * keeps the uuid index in step, and announces every password it takes with
     * it -- their cleartext would
     * otherwise outlive the entries that recorded them, with no countdown to take
     * it off under a password-timeout of 0. Nothing is published over the
     * selections here beyond that: there is no head left to publish, and the
     * ordinary text a selection holds is the user's own copy. */
    while (self->history->len)
        g_paste_history_private_remove (self, self->history->len - 1, TRUE);

    /* So are those a reload set aside: the load that was to announce what it
     * left out is given up before an emptying (g_paste_history_empty (),
     * g_paste_history_delete ()). */
    self->pending_dropped = g_slist_concat (g_steal_pointer (&self->reload_passwords), self->pending_dropped);
    self->size = 0;

    g_paste_history_private_elect_new_biggest (self);
    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REMOVE, G_PASTE_UPDATE_TARGET_ALL, 0, G_PASTE_HISTORY_SAVE_CLEAR, NULL, NULL, FALSE);
}

/* The store has been handed over: either to a successor daemon, or -- for the
 * in-shell one, which keeps answering the bus right through its migration --
 * to a migration that is reading and rewriting it as we speak. Reading or
 * writing a history behind its back would race that rewrite (and lose to it),
 * so every synchronous operation on a store refuses rather than half-do it. */
static gboolean
g_paste_history_check_owned (GPasteHistory *self,
                             const gchar   *name,
                             GError       **error)
{
    if (!self->stopped)
        return TRUE;

    g_set_error (error, G_IO_ERROR, G_IO_ERROR_BUSY,
                 "The history storage is being handed over, cannot touch “%s” right now",
                 name);
    return FALSE;
}

/* What reads or writes a store outside the model also waits for the daemon it
 * takes over from to be done writing it, as the first load does: until this
 * process holds the storage lock, a write that daemon still has out for the
 * history could land after ours and undo it. Refused rather than waited for,
 * the main thread having no business blocking on another process. */
static gboolean
g_paste_history_check_store_owned (GPasteHistory *self,
                                   const gchar   *name,
                                   GError       **error)
{
    if (!g_paste_history_check_owned (self, name, error))
        return FALSE;

    if (g_paste_storage_backend_try_lock ())
        return TRUE;

    g_set_error (error, G_IO_ERROR, G_IO_ERROR_BUSY,
                 "Another GPaste daemon is still writing the history storage, cannot touch “%s” right now",
                 name);
    return FALSE;
}

/* What empties or deletes a store checks first that the backend in use can
 * reach it. The "no storage" one cannot reach what another flavour keeps under
 * @name -- an encrypted store left locked, what a migration to it kept -- and
 * that store would bring the history back once it is used again (as in
 * g_paste_storage_backend_delete_history ()). Refused before anything changes:
 * the session's model, all that backend holds, can still be emptied one item
 * at a time. */
static gboolean
g_paste_history_check_store_reachable (GPasteHistory *self,
                                       const gchar   *name,
                                       GError       **error)
{
    if (g_paste_storage_backend_get_kind (self->backend) != G_PASTE_STORAGE_NOOP || !g_paste_util_history_is_stored_anywhere (name))
        return TRUE;

    g_set_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED,
                 "“%s” is kept by a storage not in use, which this one cannot reach", name);
    return FALSE;
}

/**
 * g_paste_history_empty:
 * @self: a #GPasteHistory instance
 * @error: return location for a #GError, or %NULL
 *
 * Empty the #GPasteHistory, in memory and in its store
 *
 * The store too whatever state the history is in: a load still in flight is
 * abandoned, since what it read would be installed right after, and a history
 * that could not be read is written over with nothing, which is what was asked
 * for -- either way, a change recorded nowhere would have the emptied items
 * back at the next start.
 *
 * Fails with %G_IO_ERROR_BUSY, having emptied nothing, when the store has been
 * handed over (see g_paste_history_flush()) or while a daemon this one took
 * over from may still be writing it (see g_paste_storage_backend_try_lock());
 * with %G_PASTE_ERROR_FAILED, having emptied nothing, when the history runs on
 * the "no storage" backend while another flavour stores it.
 *
 * Returns: whether the history was emptied
 */
G_PASTE_VISIBLE gboolean
g_paste_history_empty (GPasteHistory *self,
                       GError       **error)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), FALSE);

    G_PASTE_LOCK_HISTORY;

    /* A history nobody has named has no store to
     * reach (g_paste_history_record ()). */
    if (self->name && (!g_paste_history_check_store_owned (self, self->name, error) || !g_paste_history_check_store_reachable (self, self->name, error)))
        return FALSE;

    g_paste_history_saver_abandon_load (self->saver);
    self->unreadable = FALSE;
    g_paste_history_empty_locked (self);

    return TRUE;
}

/* The name a history is about to be loaded or saved under: the one given, or
 * the configured one. GSettings is writable by anything the user runs, so the
 * configured name is no more trusted than a name off the bus -- and a name that
 * is a path names a history file outside the history directory. Fall back to the
 * default rather than refuse to have a history at all: a setting somebody
 * mangled should not cost the user their clipboard. */
static const gchar *
g_paste_history_resolve_name (GPasteHistory *self,
                              const gchar   *name)
{
    if (!name)
        name = g_paste_settings_get_history_name (self->settings);

    const gchar *resolved = g_paste_util_resolve_history_name (name);

    /* @name is what a NULL setting resolved to, so it can still be NULL here,
     * and printf ("%s", NULL) is undefined -- it is glibc that prints
     * "(null)", not C. */
    if (resolved != name)
        g_warning ("“%s” does not name a history; using “%s” instead", (name) ?: "(none)", resolved);

    return resolved;
}

/**
 * g_paste_history_save:
 * @self: a #GPasteHistory instance
 * @name: (nullable): the name to save the history to (defaults to the configured one)
 *
 * Save the #GPasteHistory to the history file, synchronously. A save that fails
 * leaves the store of @name as it was (see
 * g_paste_storage_backend_write_history()).
 *
 * Returns: whether the store of @name holds the history now
 */
G_PASTE_VISIBLE gboolean
g_paste_history_save (GPasteHistory *self,
                      const gchar   *name)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), FALSE);

    G_PASTE_LOCK_HISTORY;

    g_autolist (GPasteItem) snapshot = g_paste_history_snapshot (self);

    /* Resolved the same way a load is: an unchecked name reaches
     * g_paste_util_get_history_file_path (), which answers %NULL for one that is
     * a path -- and a save that builds no path writes nothing at all, where a
     * load would have fallen back to the default. */
    const gchar *target = g_paste_history_resolve_name (self, (name) ? name : self->name);

    /* Another history is written through a backend of its own. The SQLite
     * flavours keep one connection per backend, and opening another history
     * through ours would close the one the saver writes this history through,
     * for its next write to open it again -- an Argon2id pass on an encrypted
     * store (/history/sqlite_save_elsewhere_keeps_connection). */
    g_autoptr (GPasteStorageBackend) elsewhere = (g_paste_str_equal (target, self->name))
        ? NULL
        : g_paste_storage_backend_new (g_paste_storage_backend_get_kind (self->backend), self->settings);

    return g_paste_storage_backend_write_history ((elsewhere) ? elsewhere : self->backend, target, snapshot);
}

/**
 * g_paste_history_flush:
 * @self: a #GPasteHistory instance
 *
 * Persist every pending change and wait for it to hit the disk, then stop
 * recording further changes. Meant for an exit/handover path: after this returns
 * the on-disk history is up to date and can be handed over to a successor daemon.
 */
G_PASTE_VISIBLE void
g_paste_history_flush (GPasteHistory *self)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));

    G_PASTE_LOCK_HISTORY;

    self->stopped = TRUE;
    g_paste_history_saver_hold_loads (self->saver, TRUE);
    g_paste_history_saver_drain (self->saver);

    /* A store a failed write left behind the history is brought level before
     * it is handed over, what the successor loads being that store: the catch
     * up a write that fails asks for (g_paste_history_on_diverged ()) is
     * recorded nowhere once stopped. One full write, which failing leaves the
     * store where it was. */
    if (self->name && !self->unreadable && !g_paste_history_saver_is_loading (self->saver) &&
        g_paste_storage_backend_has_diverged (self->backend, self->name))
    {
        g_paste_history_saver_record (self->saver, G_PASTE_HISTORY_SAVE_FULL, self->name, NULL, NULL, g_paste_history_snapshot (self), g_paste_history_stored_length (self));
        g_paste_history_saver_drain (self->saver);
    }
}

/**
 * g_paste_history_resume:
 * @self: a #GPasteHistory instance
 *
 * Undo a previous g_paste_history_flush(): resume recording changes. Meant for a
 * handover that did not actually happen (e.g. a re-exec whose exec failed), so
 * the surviving daemon keeps persisting the history.
 */
G_PASTE_VISIBLE void
g_paste_history_resume (GPasteHistory *self)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));

    G_PASTE_LOCK_HISTORY;

    self->stopped = FALSE;
    g_hash_table_remove_all (self->stopped_adds);
    /* A switch made meanwhile left its load held: this starts it. */
    g_paste_history_saver_hold_loads (self->saver, FALSE);
}

/**
 * g_paste_history_sync:
 * @self: a #GPasteHistory instance
 * @name: the history about to be read or written outside the model
 * @error: return location for a #GError, or %NULL
 *
 * Bring the store of another history than the one the model holds up to date
 * with every change recorded for it, before something reads or replaces that
 * store outside the model: the writes still out for @name land, the one in
 * flight waited for and the queued ones applied. Those of every other history
 * carry on in the background, and recording goes on. The writes of the history
 * the model holds are left where they are: what reads it reads the model, and
 * what replaces it records a write of its own, queued behind the ones already
 * out.
 *
 * Fails with %G_IO_ERROR_BUSY, having done nothing, when the store has been
 * handed over (see g_paste_history_flush()), or, for another history than the
 * one the model holds, while a daemon this one took over from may still be
 * writing it (see g_paste_storage_backend_try_lock()).
 *
 * Returns: whether @name may be read or replaced: for another history, its
 *          store being up to date; for the one the model holds, its store
 *          not having been handed over, with its writes possibly still out
 */
G_PASTE_VISIBLE gboolean
g_paste_history_sync (GPasteHistory *self,
                      const gchar   *name,
                      GError       **error)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), FALSE);
    g_return_val_if_fail (name, FALSE);

    G_PASTE_LOCK_HISTORY;

    if (g_paste_str_equal (name, self->name))
        return g_paste_history_check_owned (self, name, error);

    if (!g_paste_history_check_store_owned (self, name, error))
        return FALSE;

    g_paste_history_saver_drain_history (self->saver, name);

    return TRUE;
}

/* Read a history in, synchronously, and install it as the model.
 *
 * Its one caller is BackupHistory of a history the daemon is not keeping, which
 * saves what this read as the backup. That is what the read answers: the
 * backend's own caps applied -- no image with images-support off, the
 * non-favourites past max-history-size left out -- and nothing more, unlike the
 * load g_paste_history_on_loaded () installs, which applies the memory cap too:
 * a backup is a copy of the history as GPaste reads it, not of its store. */
static void
g_paste_history_load_locked (GPasteHistory *self,
                             const gchar   *name)
{
    g_paste_history_private_clear (self);
    self->size = 0;

    g_set_str (&self->name, g_paste_history_resolve_name (self, name));

    /* A history that is on disk but unreadable (wrong passphrase, corrupt or
     * truncated file, I/O error) must not be persisted over: stop recording so
     * the next clipboard change cannot rewrite the still-intact data from the
     * empty model we ended up with. Conversely a history that *did* read back
     * clears the flag: it is this history's own readability that decides,
     * otherwise one unreadable history would silently stop persisting every
     * other one loaded afterwards. */
    GList *history = NULL;

    self->unreadable = !g_paste_storage_backend_read_history (self->backend, self->name, NULL /* cancellable */, &history, &self->size);
    g_paste_history_private_set_from_list (self, history);

    if (self->unreadable)
        g_warning ("Could not read the history back; it will not be overwritten");

    g_paste_history_activate_first (self, TRUE);

    /* Unconditional: biggest_uuid borrows the uuid of an item we just freed, so
     * it has to be re-elected (to NULL) even when the new history is empty. */
    g_paste_history_private_elect_new_biggest (self);
}

/**
 * g_paste_history_load:
 * @self: a #GPasteHistory instance
 * @name: (nullable): the name of the history to load, defaults to the configured one
 *
 * Load the #GPasteHistory from the history file (synchronous)
 */
G_PASTE_VISIBLE void
g_paste_history_load (GPasteHistory *self,
                      const gchar   *name)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));
    g_return_if_fail (!name || g_utf8_validate (name, -1, NULL));

    G_PASTE_LOCK_HISTORY;

    if (self->name && g_paste_str_equal (name, self->name))
        return;

    g_paste_history_load_locked (self, name);
}

/* Install the result of an async load (driven by the saver) into the model. */
static void
g_paste_history_on_loaded (gpointer user_data,
                           GList   *history,
                           gsize    size,
                           gboolean save_after,
                           gboolean readable)
{
    GPasteHistory *self = user_data;
    G_PASTE_LOCK_HISTORY;

    /* What was added while the load was out -- a copy, an AddText -- is in the
     * model and nowhere else, recording waiting for the load (see
     * g_paste_history_update ()), and the read list is about to take its
     * place. Kept aside, oldest first, to be added on top of what was read. */
    g_autolist (GPasteItem) added = NULL;

    for (guint i = 0; i < self->history->len; ++i)
        added = g_list_prepend (added, g_object_ref (g_ptr_array_index (self->history, i)));

    g_paste_history_private_set_from_list (self, history);
    self->size = size;

    for (GSList *l = self->reload_passwords; l; l = l->next)
    {
        if (!g_hash_table_contains (self->by_uuid, g_paste_item_get_uuid (l->data)))
            self->pending_dropped = g_slist_prepend (self->pending_dropped, g_object_ref (l->data));
    }
    g_clear_slist (&self->reload_passwords, g_object_unref);

    /* The head read is no head the selections get when adds go on top of it:
     * the last of those is (see below), and a selection told to take the read
     * one instead would have it published over a password just copied --
     * dropped, even, as an image whose texture the adds idle. */
    g_paste_history_activate_first (self, !added);

    /* Unconditional: biggest_uuid borrows a uuid from the list we just freed. */
    g_paste_history_private_elect_new_biggest (self);

    /* The history is on disk but could not be read back: never write our empty
     * model over it (see g_paste_history_load_locked). */
    self->unreadable = !readable;

    if (self->unreadable)
    {
        g_warning ("Could not read the history back; it will not be overwritten");
        save_after = FALSE;
    }
    /* Both caps, applied to what was just read. A backend reads a favourite back
     * whatever it costs -- it counts only the items its cap could evict, or one
     * that had sunk past the cap would be dropped on load and destroyed by the
     * next save -- so what arrives here can hold more than max-history-size once
     * the favourites are counted, which is how the caps count. Applying them now
     * is what keeps the two answers the same: left to the next clipboard change,
     * the rows over the cap would sit there until it came and then vanish all at
     * once.
     *
     * The store has to drop what the model dropped, the file flavours sweeping
     * the images of what it evicted with that write: whatever the load asked
     * for, a trim is saved. Which is also why a history that is unreadable or
     * flushed is left alone: neither can be saved (the first must never be
     * written over, the second belongs to a successor daemon now), so a trim
     * there would only have the model answer for less than the store holds. A
     * switch can land right inside that flushed window -- see
     * g_paste_history_history_name_changed (). */
    else if (!self->stopped)
    {
        guint length_before = self->history->len;

        g_paste_history_private_check_size (self);
        g_paste_history_private_check_memory_usage (self);

        if (self->history->len != length_before)
            save_after = TRUE;
    }

    if (save_after)
        g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0, G_PASTE_HISTORY_SAVE_FULL, NULL, NULL, FALSE);
    else
        g_paste_history_emit_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, NULL, 0);

    /* Recorded as the adds they were, under the uuids their callers were
     * answered with, now that a load no longer holds recording back. An add
     * keeps the head it equals, and its uuid: here the head makes way, as a twin
     * further down would. */
    gboolean head_made_way = FALSE;

    for (GList *l = added; l; l = l->next)
    {
        GPasteItem *item = l->data;

        if (self->history->len && g_paste_item_equals (g_ptr_array_index (self->history, 0), item))
        {
            g_autoptr (GPasteItem) head = g_ptr_array_index (self->history, 0);

            if (g_paste_item_is_favourite (head))
                g_paste_item_set_favourite (item, TRUE);
            g_paste_history_private_remove (self, 0, FALSE);
            head_made_way = TRUE;
        }
        _g_paste_history_add (self, g_object_ref (item), TRUE);
    }

    /* A head taken out by hand is no displacement the add saw, so an
     * incremental store would keep the read head's row beside the add's: the
     * whole history is written instead, which reconciles it away. */
    if (head_made_way)
        g_paste_history_record (self, G_PASTE_HISTORY_SAVE_FULL, NULL, NULL, FALSE);

    /* The selections get the head those adds left, the last copy, rather than
     * the one the load read -- nothing, when that is a password, which is on
     * the selection it was copied from already. */
    if (added)
        g_paste_history_activate_first (self, TRUE);
}

/**
 * g_paste_history_reload_backend:
 * @self: a #GPasteHistory instance
 *
 * Rebuild the storage backend from the (possibly changed) "storage-backend"
 * setting and re-read the history from it, then resume recording. Meant to be
 * called once a storage migration or a passphrase change has rewritten the
 * store on disk: it assumes g_paste_history_flush() already stopped recording
 * and drained the saver, so the backend swap does not race any pending write.
 */
G_PASTE_VISIBLE void
g_paste_history_reload_backend (GPasteHistory *self)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));

    /* The migration ran out of process (or in a re-exec'd one), so the new
     * "storage-backend" value may not have reached our cached settings yet: read
     * it back before rebuilding, or we would resurrect the flavour we just left.
     * Before the lock: a reload announces extension-enabled when it moved, and
     * nothing is emitted under the lock. */
    g_paste_settings_reload (self->settings);

    G_PASTE_LOCK_HISTORY;

    /* What the store does not hold is in the model alone: kept in the model the
     * reload starts from, the load adds it on top of what it reads
     * (g_paste_history_on_loaded ()), recorded then. With a load still out,
     * that is the whole model, nothing having been recorded since it started.
     * Otherwise it is what was copied while the store was handed over, down to
     * the oldest copy still listed, and whatever sits above that copy: a
     * password's expiry puts the entry before it back on top. */
    guint carried = 0;

    if (self->saver && g_paste_history_saver_is_loading (self->saver))
        carried = self->history->len;
    else
    {
        for (guint i = 0; i < self->history->len; ++i)
        {
            if (g_hash_table_contains (self->stopped_adds, g_paste_item_get_uuid (g_ptr_array_index (self->history, i))))
                carried = i + 1;
        }
    }
    g_hash_table_remove_all (self->stopped_adds);

    /* A load still in flight keeps the old saver alive through the task's own
     * reference; detach it so its (pre-migration) result is dropped instead of
     * being installed over the one we are about to load. */
    if (self->saver)
        g_paste_history_saver_detach (self->saver);

    g_clear_object (&self->saver);
    g_clear_object (&self->backend);
    self->backend = g_paste_storage_backend_new (g_paste_settings_get_storage_backend (self->settings), self->settings);
    self->saver = g_paste_history_saver_new (self->backend, self, g_paste_history_on_loaded, g_paste_history_on_diverged);

    GList *kept = NULL;

    for (guint i = carried; i > 0; --i)
        kept = g_list_prepend (kept, g_object_ref (g_ptr_array_index (self->history, i - 1)));

    /* Added to what a reload before this one set aside, its load given up
     * above before it could say which of them it found again. */
    for (guint i = carried; i < self->history->len; ++i)
    {
        GPasteItem *item = g_ptr_array_index (self->history, i);

        if (G_PASTE_IS_PASSWORD_ITEM (item))
            self->reload_passwords = g_slist_prepend (self->reload_passwords, g_object_ref (item));
    }
    g_paste_history_private_set_from_list (self, kept);
    self->size = 0;
    for (guint i = 0; i < self->history->len; ++i)
        self->size += g_paste_item_get_size (g_ptr_array_index (self->history, i));
    g_paste_history_private_elect_new_biggest (self);
    /* The deliberate end of the handover g_paste_history_flush() opened: the
     * store has been rewritten and we are the one that owns it again. Whatever
     * the backend we just dropped could not read is moot too. */
    self->stopped = FALSE;
    self->unreadable = FALSE;

    /* Asynchronously, like every other load: reading (and, for an encrypted
     * flavour, deriving the key with Argon2id) inline would freeze the whole
     * compositor in the gnome-shell host, and stop the standalone daemon, which
     * reloads after a passphrase change, from answering the bus meanwhile. The
     * store was just rewritten, so there is nothing to normalize back. */
    g_paste_history_saver_load (self->saver, self->name, FALSE);

    /* Every view is told the history it shows is gone, as a switch tells it,
     * and redraws again once the load has filled it. A switch is announced by
     * the name it changes, which a reload leaves as it was. */
    g_paste_history_emit_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, NULL, 0);
}

/**
 * g_paste_history_load_async:
 * @self: a #GPasteHistory instance
 * @name: (nullable): the name of the history to load, defaults to the configured one
 *
 * Load the #GPasteHistory from the history file asynchronously.
 * The UPDATE signal is emitted on the main thread when loading is complete.
 */
G_PASTE_VISIBLE void
g_paste_history_load_async (GPasteHistory *self,
                            const gchar   *name)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));
    g_return_if_fail (!name || g_utf8_validate (name, -1, NULL));

    G_PASTE_LOCK_HISTORY;

    const gchar *resolved = g_paste_history_resolve_name (self, name);

    if (self->name && g_paste_str_equal (resolved, self->name) && !g_paste_history_saver_is_loading (self->saver))
        return;

    g_set_str (&self->name, resolved);
    g_paste_history_private_clear (self);
    self->size = 0;
    /* A previously unreadable history must not keep the *next* one from being
     * persisted: the load's own result decides (see g_paste_history_on_loaded).
     * Nothing is recorded in the meantime, since a load is now in progress.
     * @stopped is deliberately left alone — a handover is not ours to undo. */
    self->unreadable = FALSE;

    g_paste_history_saver_load (self->saver, self->name, FALSE);
}

/**
 * g_paste_history_switch:
 * @self: a #GPasteHistory instance
 * @name: the name of the new history
 *
 * Switch to a new history
 *
 * The switch is the "history-name" setting changing, which GSettings may
 * refuse to store -- a locked key, a settings file it cannot rewrite: the
 * history then stays where it was, which the answer says.
 *
 * Returns: whether @name is the history now
 */
G_PASTE_VISIBLE gboolean
g_paste_history_switch (GPasteHistory *self,
                        const gchar   *name)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), FALSE);
    g_return_val_if_fail (g_paste_util_history_name_is_valid (name), FALSE);
    g_return_val_if_fail (g_utf8_validate (name, -1, NULL), FALSE);

    g_paste_settings_set_history_name (self->settings, name);
    g_paste_settings_sync_key (self->settings, G_PASTE_HISTORY_NAME_SETTING);

    return g_paste_str_equal (g_paste_settings_get_history_name (self->settings), name);
}

/**
 * g_paste_history_delete:
 * @self: a #GPasteHistory instance
 * @name: (nullable): the history to delete (defaults to the configured one)
 * @error: return location for a #GError, or %NULL
 *
 * Delete the history called @name, the current one when %NULL. A current
 * history the history-name setting names, other than the default one, is left
 * for the default one first, and a delete that then fails goes back to it; the
 * passwords it held are announced as gone (GPasteHistory::password-dropped)
 * once it is.
 *
 * Fails with %G_IO_ERROR_BUSY, having deleted nothing, when the store has been
 * handed over (see g_paste_history_flush()) or while a daemon this one took
 * over from may still be writing it (see g_paste_storage_backend_try_lock());
 * with %G_PASTE_ERROR_FAILED, having deleted nothing, when the history-name
 * setting cannot be changed to leave the current history, or when the "no
 * storage" backend is in use while another flavour stores it; otherwise errors
 * are whatever the storage backend reports for unlinking it, in %G_IO_ERROR or
 * %G_FILE_ERROR.
 *
 * Returns: whether the history was deleted
 */
G_PASTE_VISIBLE gboolean
g_paste_history_delete (GPasteHistory *self,
                        const gchar   *name,
                        GError       **error)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), FALSE);

    /* A copy: leaving the current history below frees the name it was. */
    g_autofree gchar *history_name = g_strdup ((name) ? name : self->name);

    if (!g_paste_history_check_store_owned (self, history_name, error))
        return FALSE;

    if (!g_paste_history_check_store_reachable (self, history_name, error))
        return FALSE;

    g_autoslist (GPasteItem) passwords = NULL;
    gboolean left = FALSE;

    if (g_paste_str_equal (history_name, self->name) && !g_paste_str_equal (history_name, G_PASTE_DEFAULT_HISTORY) &&
        g_paste_str_equal (history_name, g_paste_settings_get_history_name (self->settings)))
    {
        /* The default history is where a deleted current history leaves the
         * daemon, and leaving comes first: a history-name GSettings refuses to
         * store would keep it current, and deleting it then would have the next
         * copy write it back. A switch announces no password, so those it held
         * are kept to be announced once it is gone. One the setting does not
         * name -- loaded by name -- is emptied where it is, below. */
        for (guint i = 0; i < self->history->len; ++i)
        {
            GPasteItem *item = g_ptr_array_index (self->history, i);

            if (G_PASTE_IS_PASSWORD_ITEM (item))
                passwords = g_slist_prepend (passwords, g_object_ref (item));
        }

        /* And those a reload set aside while its load is out, which the switch
         * lets go of unannounced: the store about to be unlinked held them. */
        for (GSList *l = self->reload_passwords; l; l = l->next)
            passwords = g_slist_prepend (passwords, g_object_ref (l->data));

        /* The load the switch starts waits for the unlink: on the SQLite
         * flavours it holds the backend's one lock across its whole read -- a
         * key derivation, every row -- which the unlink, on the main thread,
         * would then wait on. */
        g_paste_history_saver_hold_loads (self->saver, TRUE);

        if (!g_paste_history_switch (self, G_PASTE_DEFAULT_HISTORY))
        {
            g_paste_history_saver_hold_loads (self->saver, FALSE);
            g_set_error_literal (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED,
                                 "the history-name setting cannot be changed, so the current history cannot be deleted");
            return FALSE;
        }
        left = TRUE;
    }
    else if (g_paste_str_equal (history_name, self->name))
    {
        /* A load still in flight is reading the very history we are about to
         * unlink: its result would be installed right after, and the next
         * clipboard change would then write the whole pre-delete history back.
         * Also lets the empty() below actually record its write (a save is
         * skipped while a load is in progress). */
        g_paste_history_saver_abandon_load (self->saver);

        /* Not g_paste_history_empty (): a history that could not be read stays
         * so until its store is gone, the unlink below being free to fail. */
        G_PASTE_LOCK_HISTORY;
        g_paste_history_empty_locked (self);
    }

    /* The saver may still hold writes for this history: the emptying above, or
     * those recorded before a switch away from it, one possibly still in
     * flight. Applied *after* the file is gone, any of them would re-create it
     * (both backends create the store on open/write), so the deleted history
     * would come back in the next listing. Land them first. */
    g_paste_history_saver_drain_history (self->saver, history_name);

    g_autoptr (GError) local_error = NULL;

    g_paste_storage_backend_delete_history (self->backend, history_name, &local_error);

    if (local_error)
    {
        /* Back to the history that is still there. */
        if (left && !g_paste_history_switch (self, history_name))
            g_warning ("Could not go back to “%s”, which was not deleted", history_name);
        if (left)
            g_paste_history_saver_hold_loads (self->saver, FALSE);
        g_propagate_error (error, g_steal_pointer (&local_error));
        return FALSE;
    }

    if (left)
        g_paste_history_saver_hold_loads (self->saver, FALSE);

    /* Whatever we could not read back is unlinked, so there is nothing left to
     * protect: recording must resume, or the gate would stay shut for the rest
     * of the process and nothing would ever be persisted again. Only now,
     * a delete that failed leaving it to protect still. */
    if (g_paste_str_equal (history_name, self->name))
        self->unreadable = FALSE;

    if (passwords)
    {
        G_PASTE_LOCK_HISTORY;
        self->pending_dropped = g_slist_concat (g_steal_pointer (&passwords), self->pending_dropped);
    }

    return TRUE;
}

static void
g_paste_history_history_name_changed (GPasteHistory *self)
{
    const gchar *name = g_paste_history_resolve_name (self, NULL);

    /* The setting written with the name already in use -- a switch to the
     * history in use, which GPaste's keyfile settings announce every time, and
     * dconf the first time a key still at its default is written -- is no
     * switch: a reload would drop what only the model holds, a plain flavour's
     * passwords above all, where g_paste_history_load_async () leaves the
     * history alone. */
    if (self->name && g_paste_str_equal (name, self->name))
        return;

    g_set_str (&self->name, name);

    g_debug ("history: name changed to '%s'", self->name);

    /* What was added while a load of the history being left was out goes with
     * it, unrecorded: only the list that load was reading could have taken it
     * on top, a file history being written whole, and that load is given up. */
    g_paste_history_private_clear (self);
    g_clear_slist (&self->reload_passwords, g_object_unref);
    g_hash_table_remove_all (self->stopped_adds);
    self->size = 0;
    /* Switching away from an unreadable history resumes recording: the new one
     * stands on its own (see g_paste_history_load_async). A flushed history
     * stays flushed, though — the in-shell daemon keeps answering the bus right
     * through its migration, so a switch can land inside that window and must
     * not put the pre-migration backend back to work: its load waits for the
     * handover to end, held where the saver holds loads, and supersedes the
     * load still out for the history left right away -- installed, that one
     * would stand as the history switched to.
     *
     * Nothing is written back: a trim the load makes saves itself
     * (g_paste_history_on_loaded ()), and what a read otherwise normalizes --
     * a format upgraded, a duplicate uuid -- is written with the next change,
     * as for the load g_paste_history_load_async () makes. A write here would
     * be the whole history rewritten for every switch, re-encrypted and
     * synced. */
    self->unreadable = FALSE;

    g_paste_history_emit_switch (self, self->name);
    g_paste_history_saver_load_when_released (self->saver, self->name, FALSE);
}

/* One detailed "notify::<key>" handler per setting the history reacts to, rather
 * than one undetailed handler dispatching on the key, so the history is never
 * woken for the twenty-odd settings that mean nothing to it. */

/* Either cap: both are answered by applying both, since a trim is what the
 * history owes each of them at all times. */
static void
g_paste_history_on_cap_changed (GPasteSettings *settings G_GNUC_UNUSED,
                                GParamSpec     *pspec G_GNUC_UNUSED,
                                gpointer        user_data)
{
    GPasteHistory *self = user_data;
    G_PASTE_LOCK_HISTORY;

    g_paste_history_private_trim (self);
}

static void
g_paste_history_on_history_name_changed (GPasteSettings *settings G_GNUC_UNUSED,
                                         GParamSpec     *pspec G_GNUC_UNUSED,
                                         gpointer        user_data)
{
    GPasteHistory *self = user_data;
    G_PASTE_LOCK_HISTORY;

    g_paste_history_history_name_changed (self);
}

static void
g_paste_history_dispose (GObject *object)
{
    GPasteHistory *self = G_PASTE_HISTORY (object);

    if (self->saver)
        g_paste_history_saver_detach (self->saver);

    g_clear_object (&self->saver);
    g_clear_object (&self->backend);
    g_clear_object (&self->pending_selection);
    g_clear_slist (&self->pending_dropped, g_object_unref);
    g_clear_slist (&self->reload_passwords, g_object_unref);
    g_clear_pointer (&self->stopped_adds, g_hash_table_unref);
    g_clear_pointer (&self->history, g_ptr_array_unref);
    g_clear_pointer (&self->by_uuid, g_hash_table_unref);
    g_clear_object (&self->settings_signals);
    g_clear_object (&self->settings);

    G_OBJECT_CLASS (g_paste_history_parent_class)->dispose (object);
}

static void
g_paste_history_finalize (GObject *object)
{
    GPasteHistory *self = G_PASTE_HISTORY (object);

    g_free (self->name);
    g_mutex_clear (&self->lock);

    G_OBJECT_CLASS (g_paste_history_parent_class)->finalize (object);
}

static void
g_paste_history_class_init (GPasteHistoryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_history_dispose;
    object_class->finalize = g_paste_history_finalize;

    /**
     * GPasteHistory::selected:
     * @history: the object on which the signal was emitted
     * @item: (nullable): the new selected item, or %NULL when the selections
     *        are to be cleared
     *
     * The "selected" signal is emitted when the user has just
     * selected a new item form the history, or when removing the head leaves a
     * password there, which is not published without being asked for.
     */
    signals[SELECTED] = g_signal_new ("selected",
                                      G_PASTE_TYPE_HISTORY,
                                      G_SIGNAL_RUN_LAST,
                                      0, /* class offset */
                                      NULL, /* accumulator */
                                      NULL, /* accumulator data */
                                      g_cclosure_marshal_VOID__OBJECT,
                                      G_TYPE_NONE,
                                      1, /* number of params */
                                      G_PASTE_TYPE_ITEM);

    /**
     * GPasteHistory::password-dropped:
     * @history: the object on which the signal was emitted
     * @item: the password whose entry has just left the history
     *
     * The "password-dropped" signal is emitted when a password's entry leaves the
     * history for good -- deleted, emptied out or evicted by a cap -- from
     * whatever position it held.
     *
     * Its cleartext may still be sitting on a selection, where the entry that
     * recorded it is now gone and no countdown need ever have been running: a
     * password-timeout of 0 leaves a password on the clipboard until something
     * replaces it. Which selections carry it is not something the history can
     * answer, so this says only which password went.
     */
    signals[PASSWORD_DROPPED] = g_signal_new ("password-dropped",
                                              G_PASTE_TYPE_HISTORY,
                                              G_SIGNAL_RUN_LAST,
                                              0, /* class offset */
                                              NULL, /* accumulator */
                                              NULL, /* accumulator data */
                                              g_cclosure_marshal_VOID__OBJECT,
                                              G_TYPE_NONE,
                                              1, /* number of params */
                                              G_PASTE_TYPE_ITEM);

    /**
     * GPasteHistory::switch:
     * @history: the object on which the signal was emitted
     * @name: the new history name
     *
     * The "switch" signal is emitted when the user has just
     * switched to a new history
     */
    signals[SWITCH] = g_signal_new ("switch",
                                    G_PASTE_TYPE_HISTORY,
                                    G_SIGNAL_RUN_LAST,
                                    0, /* class offset */
                                    NULL, /* accumulator */
                                    NULL, /* accumulator data */
                                    g_cclosure_marshal_VOID__STRING,
                                    G_TYPE_NONE,
                                    1, /* number of params */
                                    G_TYPE_STRING);

    /**
     * GPasteHistory::update:
     * @history: the object on which the signal was emitted
     * @action: the kind of update
     * @target: the items which need updating
     * @uuid: the item the update is about, when the target is ITEM; "" otherwise
     * @index: where that item sits, when the target is ITEM
     *
     * The "update" signal is emitted whenever anything changed
     * in the history (something was added, removed, selected, replaced...).
     */
    signals[UPDATE] = g_signal_new ("update",
                                    G_PASTE_TYPE_HISTORY,
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
g_paste_history_init (GPasteHistory *self)
{
    g_mutex_init (&self->lock);

    /* The array owns a ref per item; the index borrows both its keys and its
     * values from it, so it gets no free funcs of its own. */
    self->history = g_ptr_array_new_with_free_func (g_object_unref);
    self->by_uuid = g_hash_table_new (g_str_hash, g_str_equal);
    self->stopped_adds = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    G_PASTE_LOCK_HISTORY;

    g_paste_history_private_elect_new_biggest (self);
}

/**
 * g_paste_history_get_history:
 * @self: a #GPasteHistory instance
 *
 * Get the inner history of a #GPasteHistory
 *
 * Returns: (element-type GPasteItem) (transfer none): The inner history,
 *          newest first
 */
G_PASTE_VISIBLE const GPtrArray *
g_paste_history_get_history (GPasteHistory *self)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);

    return self->history;
}

/**
 * g_paste_history_is_loading:
 * @self: a #GPasteHistory instance
 *
 * Whether a load is still replacing the history: what it holds until then is
 * what the load is about to replace, adds made in the meantime then being
 * added on top of what it read.
 *
 * Returns: whether a load is in flight
 */
G_PASTE_VISIBLE gboolean
g_paste_history_is_loading (GPasteHistory *self)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), FALSE);

    return g_paste_history_saver_is_loading (self->saver);
}

/**
 * g_paste_history_get_length:
 * @self: a #GPasteHistory instance
 *
 * Get the length of a #GPasteHistory
 *
 * Returns: The length of the inner history
 */
G_PASTE_VISIBLE guint64
g_paste_history_get_length (GPasteHistory *self)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), 0);

    G_PASTE_LOCK_HISTORY;

    return self->history->len;
}

/**
 * g_paste_history_get_current:
 * @self: a #GPasteHistory instance
 *
 * Get the name of the current history
 *
 * Returns: The name of the current history
 */
G_PASTE_VISIBLE const gchar *
g_paste_history_get_current (GPasteHistory *self)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), 0);

    return self->name;
}

/**
 * g_paste_history_is_unreadable:
 * @self: a #GPasteHistory instance
 *
 * Whether the history that was last loaded is on disk but could not be read
 * (wrong passphrase, corrupt or truncated file, I/O error), leaving this
 * instance holding an empty model that is not what the store actually has.
 *
 * Recording stops on its own for that case, so nothing overwrites the intact
 * data; a caller that copies the model out somewhere else -- a backup -- has to
 * ask, an empty answer being indistinguishable from a genuinely empty history.
 *
 * Returns: %TRUE when the model is empty because the read failed
 */
G_PASTE_VISIBLE gboolean
g_paste_history_is_unreadable (GPasteHistory *self)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), FALSE);

    return self->unreadable;
}

/**
 * g_paste_history_search:
 * @self: a #GPasteHistory instance
 * @pattern: the pattern to match
 *
 * Get the elements matching @pattern in the history
 *
 * Returns: (transfer full): The uuids of the matching elements
 */
G_PASTE_VISIBLE GStrv
g_paste_history_search (GPasteHistory *self,
                        const gchar   *pattern)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);
    g_return_val_if_fail (pattern && g_utf8_validate (pattern, -1, NULL), NULL);
    g_return_val_if_fail (strlen (pattern) <= G_PASTE_HISTORY_MAX_SEARCH_LENGTH, NULL);

    g_debug ("history: search '%s'", pattern);

    G_PASTE_LOCK_HISTORY;
    g_autoptr (GError) error = NULL;
    g_autoptr (GRegex) regex = g_regex_new (pattern,
                                            G_REGEX_CASELESS|G_REGEX_MULTILINE|G_REGEX_DOTALL|G_REGEX_OPTIMIZE,
                                            G_REGEX_MATCH_NOTEMPTY|G_REGEX_MATCH_NEWLINE_ANY,
                                            &error);

    if (error)
    {
        g_warning ("error while creating regex: %s", error->message);
        return NULL;
    }
    if (!regex)
        return NULL;

    g_autoptr (GStrvBuilder) results = g_strv_builder_new ();
    for (guint i = 0; i < self->history->len; ++i)
    {
        GPasteItem *item = g_ptr_array_index (self->history, i);
        const gchar *uuid = g_paste_item_get_uuid (item);
        /* An image's value is the checksum naming its file, which nobody ever
         * typed: what its row shows, its size and when it was taken, is what a
         * search finds it by. */
        const gchar *text = (g_paste_item_get_kind (item) == G_PASTE_ITEM_KIND_IMAGE) ? g_paste_item_get_display_string (item) : g_paste_item_get_value (item);
        gboolean match = FALSE;

        /* A password's value is not what it is known by, its name is: matched
         * as the value would be, whatever its case, and the placeholder its
         * value is not at all -- an asterisk typed would list every one. */
        if (g_paste_str_equal (pattern, uuid))
            match = TRUE;
        else if (G_PASTE_IS_PASSWORD_ITEM (item))
            match = g_regex_match (regex, g_paste_password_item_get_name (G_PASTE_PASSWORD_ITEM (item)), G_REGEX_MATCH_NOTEMPTY|G_REGEX_MATCH_NEWLINE_ANY, NULL);
        else if (g_regex_match (regex, text, G_REGEX_MATCH_NOTEMPTY|G_REGEX_MATCH_NEWLINE_ANY, NULL))
            match = TRUE;

        if (match)
            g_strv_builder_add (results, uuid);
    }

    return g_strv_builder_end (results);
}

/**
 * g_paste_history_new:
 * @settings: (transfer none): a #GPasteSettings instance
 *
 * Create a new instance of #GPasteHistory
 *
 * Returns: a newly allocated #GPasteHistory
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteHistory *
g_paste_history_new (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    GPasteHistory *self = g_object_new (G_PASTE_TYPE_HISTORY, NULL);

    self->backend = g_paste_storage_backend_new (g_paste_settings_get_storage_backend (settings), settings);
    self->saver = g_paste_history_saver_new (self->backend, self, g_paste_history_on_loaded, g_paste_history_on_diverged);
    self->settings = g_object_ref (settings);

    /* The text item size settings are deliberately absent: they filter what
     * gets in (see g_paste_clipboard_content_*), and narrowing them must not
     * retroactively delete items already recorded under the old bounds, the way
     * the size and memory limits below do. */
    GSignalGroup *settings_signals = self->settings_signals = g_signal_group_new (G_PASTE_TYPE_SETTINGS);
    g_signal_group_connect (settings_signals, "notify::" G_PASTE_MAX_HISTORY_SIZE_SETTING,
                            G_CALLBACK (g_paste_history_on_cap_changed), self);
    g_signal_group_connect (settings_signals, "notify::" G_PASTE_MAX_MEMORY_USAGE_SETTING,
                            G_CALLBACK (g_paste_history_on_cap_changed), self);
    g_signal_group_connect (settings_signals, "notify::" G_PASTE_HISTORY_NAME_SETTING,
                            G_CALLBACK (g_paste_history_on_history_name_changed), self);
    g_signal_group_set_target (settings_signals, settings);

    return self;
}

/**
 * g_paste_history_list:
 * @self: a #GPasteHistory instance
 * @error: return location for a #GError, or %NULL
 *
 * Get the list of available histories
 *
 * Errors are the storage backend's own, in %G_IO_ERROR or %G_FILE_ERROR.
 *
 * Returns: (transfer full): The list of history names
 *                           free it with g_strfreev
 */
G_PASTE_VISIBLE GStrv
g_paste_history_list (GPasteHistory *self,
                      GError       **error)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    return g_paste_storage_backend_list_histories (self->backend, error);
}

/**
 * g_paste_history_get_size:
 * @self: a #GPasteHistory instance
 * @name: the name of the history to size
 *
 * How many items the history called @name holds.
 *
 * The one in use answers from what is already loaded; any other is counted in
 * the store, which a backend that can count never loads at all.
 *
 * Returns: the number of items @name holds
 */
G_PASTE_VISIBLE guint64
g_paste_history_get_size (GPasteHistory *self,
                          const gchar   *name)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), 0);
    g_return_val_if_fail (name, 0);

    if (g_paste_str_equal (name, g_paste_history_get_current (self)))
        return g_paste_history_get_length (self);

    /* A history just switched away from can still have writes out, which a
     * count of its store would not see yet: what they say it holds comes first
     * (/history-ordering/count-pending). */
    guint64 count;

    if (g_paste_history_saver_count_pending (self->saver, name, &count))
        return count;

    return g_paste_storage_backend_count_history (self->backend, name);
}
