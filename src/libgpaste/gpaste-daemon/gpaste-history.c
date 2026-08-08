// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-gsettings-keys.h>
#include <gpaste-3/gpaste-update-enums.h>
#include <gpaste-3/gpaste-util.h>

#include <gpaste-daemon/gpaste-history.h>
#include <gpaste-daemon/gpaste-history-saver.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-uris-item.h>

#include <gio/gio.h>

/* Takes the history lock for the rest of the scope */
#define G_PASTE_DO_LOCK_HISTORY                 \
    g_debug ("%s: Locking history", G_STRFUNC); \
    g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&self->lock)

/* Same, and emits the pending SELECTED signal, if any, once the lock has been
 * released again: the guard is declared before the locker so that its cleanup
 * runs after the unlock.
 */
#define G_PASTE_LOCK_HISTORY                                                              \
    g_auto(GPasteHistorySelectionScope) selection_scope = { .self = self }; \
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
     * is in the history: only the storage backends call
     * g_paste_item_set_uuid(), while loading, before the item gets here. */
    GPtrArray            *history;
    GHashTable           *by_uuid;
    gsize                 size;

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

    /* Note: we never track the first (active) item here */
    const gchar          *biggest_uuid;
    guint64               biggest_size;

    /* Recorded by g_paste_history_selected and emitted by G_PASTE_LOCK_HISTORY
     * once the lock is released again */
    GPasteItem           *pending_selection;
};

G_PASTE_DEFINE_TYPE (History, history, G_TYPE_OBJECT)

enum
{
    SELECTED,
    SWITCH,
    UPDATE,

    LAST_SIGNAL
};

static guint64 signals[LAST_SIGNAL] = { 0 };

typedef struct
{
    GPasteHistory *self;
} GPasteHistorySelectionScope;

static void
g_paste_history_emit_pending_selection (GPasteHistorySelectionScope *scope)
{
    GPasteHistory *self = scope->self;
    g_autoptr (GPasteItem) item = NULL;

    {
        G_PASTE_DO_LOCK_HISTORY;

        item = g_steal_pointer (&self->pending_selection);
    }

    if (!item)
        return;

    g_debug ("history: selected");

    g_signal_emit (self,
                   signals[SELECTED],
                   0, /* detail */
                   item,
                   NULL);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (GPasteHistorySelectionScope, g_paste_history_emit_pending_selection)

static void
g_paste_history_private_elect_new_biggest (GPasteHistory *self)
{
    g_debug ("history: elect biggest");

    self->biggest_uuid = NULL;
    self->biggest_size = 0;

    /* From 1: the first (active) item is never tracked. */
    for (guint i = 1; i < self->history->len; ++i)
    {
        GPasteItem *item = g_ptr_array_index (self->history, i);
        guint64 size = g_paste_item_get_size (item);

        if (size > self->biggest_size)
        {
            self->biggest_uuid = g_paste_item_get_uuid (item);
            self->biggest_size = size;
        }
    }
}

static void
g_paste_history_item_free (gpointer data)
{
    g_autoptr (GPasteItem) item = data;

    if (_G_PASTE_IS_IMAGE_ITEM (item))
        g_paste_image_item_delete_files (g_paste_item_get_value (item));
}

/* Drop the item at @index from the model. The array's ref is *stolen* rather
 * than released, so this never frees on its own: with @remove_leftovers the
 * item (and any backing file it owns) is disposed of here, without it the
 * caller has taken the ref over — either to free it itself or because the item
 * is only being moved to the front. */
static void
g_paste_history_private_remove (GPasteHistory *self,
                                guint                 index,
                                gboolean              remove_leftovers)
{
    g_return_if_fail (index < self->history->len);

    GPasteItem *item = g_ptr_array_index (self->history, index);

    self->size -= g_paste_item_get_size (item);

    /* Before the steal: the key belongs to the item. */
    g_hash_table_remove (self->by_uuid, g_paste_item_get_uuid (item));
    g_ptr_array_steal_index (self->history, index);

    if (remove_leftovers)
        g_paste_history_item_free (item);
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
g_paste_history_snapshot (const GPasteHistory *self)
{
    GList *snapshot = NULL;

    for (guint i = self->history->len; i > 0; --i)
        snapshot = g_list_prepend (snapshot, g_object_ref (g_ptr_array_index (self->history, i - 1)));

    return snapshot;
}

/* Drop every item, keeping the containers. Releases the array's refs through
 * its free func (a plain unref: unlike g_paste_history_empty this is a history
 * being swapped out, not thrown away, so backing files are left alone). */
static void
g_paste_history_private_clear (GPasteHistory *self)
{
    g_ptr_array_set_size (self->history, 0);
    g_hash_table_remove_all (self->by_uuid);
}

/* Take over an item list from the storage layer (transfer full) and install it
 * as the model, rebuilding the uuid index with it. */
static void
g_paste_history_private_set_from_list (GPasteHistory *self,
                                       GList                *history)
{
    g_paste_history_private_clear (self);

    for (GList *h = history; h; h = g_list_next (h))
    {
        GPasteItem *item = h->data;

        g_ptr_array_add (self->history, item);
        g_hash_table_insert (self->by_uuid, (gpointer) g_paste_item_get_uuid (item), item);
    }

    /* The refs moved into the array; only the links are ours to free. */
    g_list_free (history);
}

static void
g_paste_history_emit_update (GPasteHistory     *self,
                             GPasteUpdateAction action,
                             GPasteUpdateTarget target,
                             guint64            position)
{
    g_debug ("history: update");

    g_signal_emit (self,
                   signals[UPDATE],
                   0, /* detail */
                   action,
                   target,
                   position,
                   NULL);
}

static void
g_paste_history_update (GPasteHistory      *self,
                        GPasteUpdateAction  action,
                        GPasteUpdateTarget  target,
                        guint64             position,
                        GPasteHistorySaveOp op,
                        const GPasteItem   *item,
                        const gchar        *uuid)
{
    /* Don't persist intermediate states while an async load is replacing the
     * history (the load result triggers its own save when appropriate), nor once
     * we have been flushed for handover (a successor daemon owns the file now). */
    if (!self->stopped && !self->unreadable && !g_paste_history_saver_is_loading (self->saver))
    {
        /* An incremental backend only ever consumes the snapshot on an add (to
         * reconcile dedups and evictions that ride along with it); skip the
         * per-item ref-copy for the operations that would just free it. */
        GList *snapshot = (op == G_PASTE_HISTORY_SAVE_ADD || !g_paste_storage_backend_is_incremental (self->backend))
            ? g_paste_history_snapshot (self)
            : NULL;

        g_paste_history_saver_record (self->saver, op, self->name, item, uuid, snapshot);
    }

    g_paste_history_emit_update (self, action, target, position);
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

    if (select)
        g_paste_history_selected (self, first);
}

static GPasteItem *
g_paste_history_private_get_by_uuid (const GPasteHistory *self,
                                     const gchar                *uuid)
{
    return (uuid) ? g_hash_table_lookup (self->by_uuid, uuid) : NULL;
}

/* The item plus where it currently sits.
 *
 * The position is looked up rather than indexed, because indexing it would cost
 * more than it saves: adding prepends, which shifts every position, so a
 * uuid->position map would have to be rewritten on every single add. This scan
 * compares pointers, where the old list walk compared uuid strings. */
static GPasteItem *
g_paste_history_private_get_indexed_by_uuid (const GPasteHistory *self,
                                             const gchar                *uuid,
                                             guint                      *index)
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

static void
g_paste_history_private_check_memory_usage (GPasteHistory *self)
{
    guint64 max_memory = g_paste_settings_get_max_memory_usage (self->settings) * 1024 * 1024;

    while (self->size > max_memory && self->biggest_uuid)
    {
        guint index;

        if (g_paste_history_private_get_indexed_by_uuid (self, self->biggest_uuid, &index))
            g_paste_history_private_remove (self, index, TRUE);

        /* Also re-points biggest_uuid, which borrowed its string from the item
         * just freed. */
        g_paste_history_private_elect_new_biggest (self);
    }
}

static void
g_paste_history_private_check_size (GPasteHistory *self)
{
    guint64 max_history_size = g_paste_settings_get_max_history_size (self->settings);

    /* Truncate from the tail: private_remove already does the size bookkeeping,
     * the uuid index and the leftover backing files. */
    while (self->history->len > max_history_size)
        g_paste_history_private_remove (self, self->history->len - 1, TRUE);
}

static gboolean
g_paste_history_private_is_growing_line (GPasteHistory *self,
                                         GPasteItem           *old,
                                         GPasteItem           *new)
{
    if (_G_PASTE_IS_IMAGE_ITEM (old) || _G_PASTE_IS_IMAGE_ITEM (new))
        return FALSE;

    if (!(g_paste_settings_get_growing_lines (self->settings) &&
        _G_PASTE_IS_TEXT_ITEM (old) && _G_PASTE_IS_TEXT_ITEM (new) &&
        !_G_PASTE_IS_PASSWORD_ITEM (old) && !_G_PASTE_IS_PASSWORD_ITEM (new)))
            return FALSE;

    const gchar *n = g_paste_item_get_value (new);
    const gchar *o = g_paste_item_get_value (old);

    return (g_str_has_prefix (n, o) || g_str_has_suffix (n, o));
}

static void
_g_paste_history_add (GPasteHistory *self,
                      GPasteItem    *item,
                      gboolean       new_selection)
{
    g_return_if_fail (_G_PASTE_IS_HISTORY (self));
    g_return_if_fail (_G_PASTE_IS_ITEM (item));

    guint64 max_memory = g_paste_settings_get_max_memory_usage (self->settings) * 1024 * 1024;

    /* On add (new_selection) we own @item (transfer full) and must consume it on
     * every path; on select it is borrowed (already in the history) and merely
     * moved. Holding it here frees it automatically on the early-return paths;
     * it is g_steal_pointer'd into the list once it is actually stored. */
    g_autoptr (GPasteItem) owned = new_selection ? item : NULL;

    if (g_paste_item_get_size (item) > max_memory)
        return;

    gboolean election_needed = !self->history->len; // If we don't have an history we want to initalize the biggest
    GPasteUpdateTarget target = G_PASTE_UPDATE_TARGET_ALL;

    g_debug ("history: add");

    if (self->history->len)
    {
        GPasteItem *old_first = g_ptr_array_index (self->history, 0);

        if (g_paste_item_equals (old_first, item))
            return;

        if (new_selection && g_paste_history_private_is_growing_line (self, old_first, item))
        {
            if (g_paste_str_equal (self->biggest_uuid, g_paste_item_get_uuid (old_first)))
                election_needed = TRUE;
            target = G_PASTE_UPDATE_TARGET_POSITION;
            /* old_first is a distinct object replaced by the grown item; free it
             * (its shared backing file, if any, is kept). */
            g_autoptr (GPasteItem) dropped = old_first;
            g_paste_history_private_remove (self, 0, FALSE);
        }
        else
        {
            /* size may change when state is idle */
            self->size -= g_paste_item_get_size (old_first);
            g_paste_item_set_state (old_first, G_PASTE_ITEM_STATE_IDLE);

            guint64 size = g_paste_item_get_size (old_first);

            self->size += size;

            if (size >= self->biggest_size)
            {
                self->biggest_uuid = g_paste_item_get_uuid (old_first);
                self->biggest_size = size;
            }

            /* Dedup is by value, not by uuid, so the index cannot help here. */
            for (guint i = 1; i < self->history->len; ++i)
            {
                GPasteItem *entry = g_ptr_array_index (self->history, i);

                if (g_paste_item_equals (entry, item) || (new_selection && g_paste_history_private_is_growing_line (self, entry, item)))
                {
                    if (g_paste_str_equal (self->biggest_uuid, g_paste_item_get_uuid (entry)))
                        election_needed = TRUE;
                    /* On add, @entry is a distinct duplicate to free (its shared
                     * backing file is kept). On select, @entry IS @item being moved
                     * to the front, so its ref must be left alone. */
                    g_autoptr (GPasteItem) dropped = new_selection ? entry : NULL;
                    g_paste_history_private_remove (self, i, FALSE);
                    break;
                }
            }
        }
    }

    /* An image's canonical cache path is per history, so the same image copied
     * in several histories never shares (and never cross-deletes) a file:
     * anchor it under ours before it gets persisted. A select re-anchors to
     * the same path, and loaded items keep the path they were stored with. */
    if (_G_PASTE_IS_IMAGE_ITEM (item) && new_selection)
        g_paste_image_item_set_history (G_PASTE_IMAGE_ITEM (item), self->name);

    g_ptr_array_insert (self->history, 0, item);
    g_hash_table_insert (self->by_uuid, (gpointer) g_paste_item_get_uuid (item), item);
    g_steal_pointer (&owned); /* ownership transferred to the history */

    g_paste_history_activate_first (self, FALSE);
    self->size += g_paste_item_get_size (item);

    g_paste_history_private_check_size (self);

    if (election_needed)
        g_paste_history_private_elect_new_biggest (self);

    g_paste_history_private_check_memory_usage (self);
    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, target, 0, G_PASTE_HISTORY_SAVE_ADD, item, NULL);
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
    g_return_if_fail (_G_PASTE_IS_HISTORY (self));
    g_return_if_fail (_G_PASTE_IS_ITEM (item));

    G_PASTE_LOCK_HISTORY;

    _g_paste_history_add (self, item, TRUE);
}

static void
g_paste_history_remove_common (GPasteHistory        *self,
                               GPasteItem           *item,
                               guint                 index)
{
    if (!item)
        return;

    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (item));
    gboolean was_biggest = g_paste_str_equal (self->biggest_uuid, uuid);

    g_paste_history_private_remove (self, index, TRUE);

    if (!index)
        g_paste_history_activate_first (self, TRUE);

    if (was_biggest)
        g_paste_history_private_elect_new_biggest (self);

    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REMOVE, G_PASTE_UPDATE_TARGET_POSITION, index, G_PASTE_HISTORY_SAVE_REMOVE, NULL, uuid);
}

static void
g_paste_history_remove_locked (GPasteHistory        *self,
                               guint64               index)
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
    g_return_if_fail (_G_PASTE_IS_HISTORY (self));

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
    g_return_val_if_fail (_G_PASTE_IS_HISTORY (self), FALSE);

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
g_paste_history_private_get (const GPasteHistory *self,
                             guint64                     index)
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
 * Returns: a read-only #GPasteItem
 */
G_PASTE_VISIBLE const GPasteItem *
g_paste_history_get (GPasteHistory *self,
                     guint64        index)
{
    g_return_val_if_fail (_G_PASTE_IS_HISTORY (self), NULL);

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
 * Returns: a read-only #GPasteItem
 */
G_PASTE_VISIBLE const GPasteItem *
g_paste_history_get_by_uuid (GPasteHistory *self,
                             const gchar   *uuid)
{
    g_return_val_if_fail (_G_PASTE_IS_HISTORY (self), NULL);

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
    g_return_val_if_fail (_G_PASTE_IS_HISTORY (self), NULL);

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
    g_return_val_if_fail (_G_PASTE_IS_HISTORY (self), FALSE);
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
    gboolean was_biggest = g_paste_str_equal (self->biggest_uuid, old_uuid);

    self->size -= g_paste_item_get_size (old);
    self->size += g_paste_item_get_size (new);

    /* Swap in place: the steal hands back the array's ref (its free func does
     * not run) so we drop it ourselves, exactly as before, and @new takes the
     * same slot. Unlike a removal this keeps any backing file, since only the
     * item wrapping it is being replaced. */
    g_hash_table_remove (self->by_uuid, old_uuid);
    g_ptr_array_steal_index (self->history, index);
    g_object_unref (old);
    g_ptr_array_insert (self->history, index, new);
    g_hash_table_insert (self->by_uuid, (gpointer) g_paste_item_get_uuid (new), new);

    if (was_biggest)
        g_paste_history_private_elect_new_biggest (self);

    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_POSITION, index, G_PASTE_HISTORY_SAVE_REPLACE, new, old_uuid);
}

/**
 * g_paste_history_replace:
 * @self: a #GPasteHistory instance
 * @uuid: the uuid of the #GPasteTextItem to replace
 * @contents: the new contents
 *
 * Replace the contents of text item at index @index
 */
G_PASTE_VISIBLE void
g_paste_history_replace (GPasteHistory *self,
                         const gchar   *uuid,
                         const gchar   *contents)
{
    g_return_if_fail (_G_PASTE_IS_HISTORY (self));
    g_return_if_fail (!contents || g_utf8_validate (contents, -1, NULL));

    G_PASTE_LOCK_HISTORY;
    guint index;
    GPasteItem *item = g_paste_history_private_get_indexed_by_uuid (self, uuid, &index);

    if (!item)
        return;

    g_return_if_fail (_G_PASTE_IS_TEXT_ITEM (item) && g_paste_str_equal (g_paste_item_get_kind (item), "Text"));

    GPasteItem *new = g_paste_text_item_new (contents);

    _g_paste_history_replace (self, index, new);

    if (!index)
        g_paste_history_selected (self, new);
}

static GPasteItem *
_g_paste_history_private_get_password (const GPasteHistory *self,
                                       const gchar                *name,
                                       guint64                    *index)
{
    for (guint idx = 0; idx < self->history->len; ++idx)
    {
        GPasteItem *i = g_ptr_array_index (self->history, idx);

        if (_G_PASTE_IS_PASSWORD_ITEM (i) &&
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
 * g_paste_history_set_password:
 * @self: a #GPasteHistory instance
 * @uuid: the uuid of the #GPasteTextItem to change as password
 * @name: (nullable): the name to give to the password
 *
 * Mark a text item as password
 */
G_PASTE_VISIBLE void
g_paste_history_set_password (GPasteHistory *self,
                              const gchar   *uuid,
                              const gchar   *name)
{
    g_return_if_fail (_G_PASTE_IS_HISTORY (self));
    g_return_if_fail (!name || g_utf8_validate (name, -1, NULL));

    G_PASTE_LOCK_HISTORY;
    guint index;
    GPasteItem *item = g_paste_history_private_get_indexed_by_uuid (self, uuid, &index);

    g_return_if_fail (item);
    g_return_if_fail (_G_PASTE_IS_TEXT_ITEM (item) && g_paste_str_equal (g_paste_item_get_kind (item), "Text"));
    g_return_if_fail (!_g_paste_history_private_get_password (self, name, NULL));

    GPasteItem *password = g_paste_password_item_new (name, g_paste_item_get_real_value (item));

    _g_paste_history_replace (self, index, password);
}

/**
 * g_paste_history_get_password:
 * @self: a #GPasteHistory instance
 * @name: the name of the #GPastePasswordItem
 *
 * Get the first password matching name
 *
 * Returns: (nullable): a #GPastePasswordItem or %NULL
 */
G_PASTE_VISIBLE const GPastePasswordItem *
g_paste_history_get_password (GPasteHistory *self,
                              const gchar   *name)
{
    g_return_val_if_fail (_G_PASTE_IS_HISTORY (self), NULL);
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
 */
G_PASTE_VISIBLE void
g_paste_history_delete_password (GPasteHistory *self,
                                 const gchar   *name)
{
    g_return_if_fail (_G_PASTE_IS_HISTORY (self));
    g_return_if_fail (!name || g_utf8_validate (name, -1, NULL));

    G_PASTE_LOCK_HISTORY;
    guint64 index;

    if (_g_paste_history_private_get_password (self, name, &index))
        g_paste_history_remove_locked (self, index);
}

/**
 * g_paste_history_rename_password:
 * @self: a #GPasteHistory instance
 * @old_name: the old name of the #GPastePasswordItem
 * @new_name: (nullable): the new name of the #GPastePasswordItem
 *
 * Rename the password item
 */
G_PASTE_VISIBLE void
g_paste_history_rename_password (GPasteHistory *self,
                                 const gchar   *old_name,
                                 const gchar   *new_name)
{
    g_return_if_fail (_G_PASTE_IS_HISTORY (self));
    g_return_if_fail (!old_name || g_utf8_validate (old_name, -1, NULL));
    g_return_if_fail (!new_name || g_utf8_validate (new_name, -1, NULL));

    G_PASTE_LOCK_HISTORY;
    guint64 index = 0;
    GPasteItem *item = _g_paste_history_private_get_password (self, old_name, &index);
    if (item)
    {
        g_paste_password_item_set_name (G_PASTE_PASSWORD_ITEM (item), new_name);
        g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_POSITION, index, G_PASTE_HISTORY_SAVE_REPLACE, item, g_paste_item_get_uuid (item));
    }
}

/**
 * g_paste_history_empty:
 * @self: a #GPasteHistory instance
 *
 * Empty the #GPasteHistory
 */
G_PASTE_VISIBLE void
g_paste_history_empty (GPasteHistory *self)
{
    g_return_if_fail (_G_PASTE_IS_HISTORY (self));

    G_PASTE_LOCK_HISTORY;

    /* Through private_remove rather than the array's own free func: emptying
     * also drops each item's backing file, and keeps the uuid index in step. */
    while (self->history->len)
        g_paste_history_private_remove (self, self->history->len - 1, TRUE);

    self->size = 0;

    g_paste_history_private_elect_new_biggest (self);
    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REMOVE, G_PASTE_UPDATE_TARGET_ALL, 0, G_PASTE_HISTORY_SAVE_CLEAR, NULL, NULL);
}

/**
 * g_paste_history_save:
 * @self: a #GPasteHistory instance
 * @name: (nullable): the name to save the history to (defaults to the configured one)
 *
 * Save the #GPasteHistory to the history file
 */
G_PASTE_VISIBLE void
g_paste_history_save (GPasteHistory *self,
                      const gchar   *name)
{
    g_return_if_fail (_G_PASTE_IS_HISTORY (self));

    G_PASTE_LOCK_HISTORY;

    g_autolist (GPasteItem) snapshot = g_paste_history_snapshot (self);

    g_paste_storage_backend_write_history (self->backend, (name) ? name : self->name, snapshot);
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
    g_return_if_fail (_G_PASTE_IS_HISTORY (self));

    G_PASTE_LOCK_HISTORY;

    self->stopped = TRUE;
    g_paste_history_saver_drain (self->saver);
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
    g_return_if_fail (_G_PASTE_IS_HISTORY (self));

    G_PASTE_LOCK_HISTORY;

    self->stopped = FALSE;
}

static void
g_paste_history_load_locked (GPasteHistory        *self,
                             const gchar          *name)
{
    g_paste_history_private_clear (self);
    self->size = 0;

    g_set_str (&self->name, (name) ? name : g_paste_settings_get_history_name (self->settings));

    /* A history that is on disk but unreadable (wrong passphrase, corrupt or
     * truncated file, I/O error) must not be persisted over: stop recording so
     * the next clipboard change cannot rewrite the still-intact data from the
     * empty model we ended up with. Conversely a history that *did* read back
     * clears the flag: it is this history's own readability that decides,
     * otherwise one unreadable history would silently stop persisting every
     * other one loaded afterwards. */
    GList *history = NULL;

    self->unreadable = !g_paste_storage_backend_read_history (self->backend, self->name, &history, &self->size);
    g_paste_history_private_set_from_list (self, history);

    if (self->unreadable)
        g_warning ("Could not read the history back; it will not be overwritten");

    if (self->history->len)
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
    g_return_if_fail (_G_PASTE_IS_HISTORY (self));
    g_return_if_fail (!name || g_utf8_validate (name, -1, NULL));

    G_PASTE_LOCK_HISTORY;

    if (self->name && g_paste_str_equal (name, self->name))
        return;

    g_paste_history_load_locked (self, name);
}

/* Install the result of an async load (driven by the saver) into the model. */
static void
g_paste_history_on_loaded (gpointer  user_data,
                           GList    *history,
                           gsize     size,
                           gboolean  save_after,
                           gboolean  readable)
{
    GPasteHistory *self = user_data;
    G_PASTE_LOCK_HISTORY;

    g_paste_history_private_set_from_list (self, history);
    self->size = size;

    if (self->history->len)
        g_paste_history_activate_first (self, TRUE);

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

    if (save_after)
        g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0, G_PASTE_HISTORY_SAVE_FULL, NULL, NULL);
    else
        g_paste_history_emit_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0);
}

/**
 * g_paste_history_reload_backend:
 * @self: a #GPasteHistory instance
 *
 * Rebuild the storage backend from the (possibly changed) "storage-backend"
 * setting and re-read the history from it, then resume recording. Meant to be
 * called after a storage migration has rewritten the store on disk: it assumes
 * g_paste_history_flush() already stopped recording and drained the saver, so
 * the backend swap does not race any pending write.
 */
G_PASTE_VISIBLE void
g_paste_history_reload_backend (GPasteHistory *self)
{
    g_return_if_fail (_G_PASTE_IS_HISTORY (self));

    G_PASTE_LOCK_HISTORY;

    /* The migration ran out of process (or in a re-exec'd one), so the new
     * "storage-backend" value may not have reached our cached settings yet: read
     * it back before rebuilding, or we would resurrect the flavour we just left. */
    g_paste_settings_reload (self->settings);

    /* A load still in flight keeps the old saver alive through the task's own
     * reference; detach it so its (pre-migration) result is dropped instead of
     * being installed over the one we are about to load. */
    if (self->saver)
        g_paste_history_saver_detach (self->saver);

    g_clear_object (&self->saver);
    g_clear_object (&self->backend);
    self->backend = g_paste_storage_backend_new (g_paste_settings_get_storage_backend (self->settings), self->settings);
    self->saver = g_paste_history_saver_new (self->backend, self, g_paste_history_on_loaded);

    g_paste_history_private_clear (self);
    self->size = 0;
    g_paste_history_private_elect_new_biggest (self);
    /* The deliberate end of the handover g_paste_history_flush() opened: the
     * store has been rewritten and we are the one that owns it again. Whatever
     * the backend we just dropped could not read is moot too. */
    self->stopped = FALSE;
    self->unreadable = FALSE;

    /* Tell every UI to reload the whole history from the new backend. */
    g_paste_history_emit_switch (self, self->name);

    /* Asynchronously, like every other load: the only caller is the gnome-shell
     * host, where reading (and, for an encrypted flavour, deriving the key with
     * Argon2id) inline would freeze the whole compositor. The store was just
     * written by the migration, so there is nothing to normalize back. */
    g_paste_history_saver_load (self->saver, self->name, FALSE);
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
    g_return_if_fail (_G_PASTE_IS_HISTORY (self));
    g_return_if_fail (!name || g_utf8_validate (name, -1, NULL));

    G_PASTE_LOCK_HISTORY;

    const gchar *resolved = (name) ? name : g_paste_settings_get_history_name (self->settings);

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
 */
G_PASTE_VISIBLE void
g_paste_history_switch (GPasteHistory *self,
                        const gchar   *name)
{
    g_return_if_fail (_G_PASTE_IS_HISTORY (self));
    g_return_if_fail (name);
    g_return_if_fail (g_utf8_validate (name, -1, NULL));

    g_paste_settings_set_history_name (self->settings, name);
}

/**
 * g_paste_history_delete:
 * @self: a #GPasteHistory instance
 * @name: (nullable): the history to delete (defaults to the configured one)
 * @error: a #GError, set to %G_IO_ERROR_BUSY when the store has been handed over
 *         (see g_paste_history_flush()) and nothing was deleted
 *
 * Delete the current #GPasteHistory
 *
 * Returns: whether the history was deleted
 */
G_PASTE_VISIBLE gboolean
g_paste_history_delete (GPasteHistory *self,
                        const gchar   *name,
                        GError       **error)
{
    g_return_val_if_fail (_G_PASTE_IS_HISTORY (self), FALSE);

    const gchar *history_name = (name) ? name : self->name;

    /* We have handed the store over: either to a successor daemon, or — for the
     * in-shell one, which keeps answering the bus right through its migration —
     * to a migration that is reading and rewriting it as we speak. Deleting an
     * entry from underneath it would race that rewrite (and lose to it), so
     * refuse rather than half-do it. */
    if (self->stopped)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_BUSY,
                     "The history storage is being handed over, cannot delete “%s” right now",
                     history_name);
        return FALSE;
    }

    if (g_paste_str_equal (history_name, self->name))
    {
        /* A load still in flight is reading the very history we are about to
         * unlink: its result would be installed right after, and the next
         * clipboard change would then write the whole pre-delete history back.
         * Also lets the empty() below actually record its write (a save is
         * skipped while a load is in progress). */
        g_paste_history_saver_abandon_load (self->saver);

        g_paste_history_empty (self);
        /* Emptying records a write for this very history, which the saver would
         * otherwise apply *after* the file is gone — re-creating it (both
         * backends create the store on open/write), so the deleted history would
         * come back, empty, in the next listing. Land it first. */
        g_paste_history_saver_drain (self->saver);

        /* Whatever we could not read back is about to be unlinked, so there is
         * nothing left to protect: recording must resume, or the gate would stay
         * shut for the rest of the process and nothing would ever be persisted
         * again. */
        self->unreadable = FALSE;
    }

    g_autoptr (GError) local_error = NULL;

    g_paste_storage_backend_delete_history (self->backend, history_name, &local_error);

    if (local_error)
    {
        g_propagate_error (error, g_steal_pointer (&local_error));
        return FALSE;
    }

    return TRUE;
}

static void
g_paste_history_history_name_changed (GPasteHistory *self)
{
    g_set_str (&self->name, g_paste_settings_get_history_name (self->settings));

    g_debug ("history: name changed to '%s'", self->name);

    g_paste_history_private_clear (self);
    self->size = 0;
    /* Switching away from an unreadable history resumes recording: the new one
     * stands on its own (see g_paste_history_load_async). A flushed history
     * stays flushed, though — the in-shell daemon keeps answering the bus right
     * through its migration, so a switch can land inside that window and must
     * not put the pre-migration backend back to work. */
    self->unreadable = FALSE;

    g_paste_history_emit_switch (self, self->name);
    g_paste_history_saver_load (self->saver, self->name, TRUE);
}

/* One detailed "notify::<key>" handler per setting the history reacts to, rather
 * than one undetailed handler dispatching on the key: the history no longer wakes
 * up for the twenty-odd settings that mean nothing to it. */

static void
g_paste_history_on_max_history_size_changed (GPasteSettings *settings G_GNUC_UNUSED,
                                             GParamSpec     *pspec G_GNUC_UNUSED,
                                             gpointer        user_data)
{
    GPasteHistory *self = user_data;
    G_PASTE_LOCK_HISTORY;

    g_paste_history_private_check_size (self);
}

static void
g_paste_history_on_max_memory_usage_changed (GPasteSettings *settings G_GNUC_UNUSED,
                                             GParamSpec     *pspec G_GNUC_UNUSED,
                                             gpointer        user_data)
{
    GPasteHistory *self = user_data;
    G_PASTE_LOCK_HISTORY;

    g_paste_history_private_check_memory_usage (self);
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

    g_clear_object (&self->saver);
    g_clear_object (&self->backend);
    g_clear_object (&self->pending_selection);
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
     * @item: the new selected item
     *
     * The "selected" signal is emitted when the user has just
     * selected a new item form the history.
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
     * @index: the index of the item, when the target is POSITION
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
                                    3, /* number of params */
                                    G_PASTE_TYPE_UPDATE_ACTION,
                                    G_PASTE_TYPE_UPDATE_TARGET,
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
g_paste_history_get_history (const GPasteHistory *self)
{
    g_return_val_if_fail (_G_PASTE_IS_HISTORY (self), NULL);

    return self->history;
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
    g_return_val_if_fail (_G_PASTE_IS_HISTORY (self), 0);

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
g_paste_history_get_current (const GPasteHistory *self)
{
    g_return_val_if_fail (_G_PASTE_IS_HISTORY (self), 0);

    return self->name;
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
    g_return_val_if_fail (_G_PASTE_IS_HISTORY (self), NULL);
    g_return_val_if_fail (pattern && g_utf8_validate (pattern, -1, NULL), NULL);
    g_return_val_if_fail (strlen (pattern) <= 256, NULL);

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

    g_autoptr (GArray) results = g_array_new (TRUE, /* zero-terminated */
                                              TRUE, /* clear */
                                              sizeof (gchar *));
    for (guint i = 0; i < self->history->len; ++i)
    {
        const GPasteItem *item = g_ptr_array_index (self->history, i);
        const gchar *uuid = g_paste_item_get_uuid (item);
        gboolean match = FALSE;

        if (g_paste_str_equal (pattern, uuid))
            match = TRUE;
        else if (_G_PASTE_IS_PASSWORD_ITEM (item) && g_paste_str_equal (pattern, g_paste_password_item_get_name (_G_PASTE_PASSWORD_ITEM (item))))
            match = TRUE;
        else if (g_regex_match (regex, g_paste_item_get_value (item), G_REGEX_MATCH_NOTEMPTY|G_REGEX_MATCH_NEWLINE_ANY, NULL))
            match = TRUE;

        if (match)
        {
            gchar *id = g_strdup (uuid);
            g_array_append_val (results, id);
        }
    }

    return g_array_steal (results, NULL);
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
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);

    GPasteHistory *self = g_object_new (G_PASTE_TYPE_HISTORY, NULL);

    self->backend = g_paste_storage_backend_new (g_paste_settings_get_storage_backend (settings), settings);
    self->saver = g_paste_history_saver_new (self->backend, self, g_paste_history_on_loaded);
    self->settings = g_object_ref (settings);

    /* The text item size settings are deliberately absent: they filter what
     * gets in (see g_paste_clipboard_content_*), and narrowing them must not
     * retroactively delete items already recorded under the old bounds, the way
     * the size and memory limits below do. */
    GSignalGroup *settings_signals = self->settings_signals = g_signal_group_new (G_PASTE_TYPE_SETTINGS);
    g_signal_group_connect (settings_signals, "notify::" G_PASTE_MAX_HISTORY_SIZE_SETTING,
                            G_CALLBACK (g_paste_history_on_max_history_size_changed), self);
    g_signal_group_connect (settings_signals, "notify::" G_PASTE_MAX_MEMORY_USAGE_SETTING,
                            G_CALLBACK (g_paste_history_on_max_memory_usage_changed), self);
    g_signal_group_connect (settings_signals, "notify::" G_PASTE_HISTORY_NAME_SETTING,
                            G_CALLBACK (g_paste_history_on_history_name_changed), self);
    g_signal_group_set_target (settings_signals, settings);

    return self;
}

/**
 * g_paste_history_list:
 * @self: a #GPasteHistory instance
 * @error: a #GError
 *
 * Get the list of available histories
 *
 * Returns: (transfer full): The list of history names
 *                           free it with g_array_unref
 */
G_PASTE_VISIBLE GStrv
g_paste_history_list (GPasteHistory *self,
                      GError       **error)
{
    g_return_val_if_fail (_G_PASTE_IS_HISTORY (self), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    return g_paste_storage_backend_list_histories (self->backend, error);
}
