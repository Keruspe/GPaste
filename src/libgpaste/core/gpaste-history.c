/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-history.h>
#include <gpaste-image-item.h>
#include <gpaste-gsettings-keys.h>
#include <gpaste-storage-backend.h>
#include <gpaste-update-enums.h>
#include <gpaste-uris-item.h>
#include <gpaste-util.h>

struct _GPasteHistory
{
    GObject parent_instance;
};

enum
{
    C_CHANGED,

    C_LAST_SIGNAL
};

typedef struct
{
    GPasteStorageBackend *backend;
    GPasteSettings       *settings;
    GList                *history;
    guint64               size;

    gchar                *name;

    /* Note: we never track the first (active) item here */
    const gchar          *biggest_uuid;
    guint64               biggest_size;

    guint64               c_signals[C_LAST_SIGNAL];
} GPasteHistoryPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (History, history, G_TYPE_OBJECT)

enum
{
    SELECTED,
    SWITCH,
    UPDATE,

    LAST_SIGNAL
};

static guint64 signals[LAST_SIGNAL] = { 0 };

static void
g_paste_history_private_elect_new_biggest (GPasteHistoryPrivate *priv)
{
    g_debug ("history: elect biggest");

    priv->biggest_uuid = NULL;
    priv->biggest_size = 0;

    GList *history = priv->history;

    if (history)
    {
        for (history = history->next; history; history = history->next)
        {
            GPasteItem *item = history->data;
            guint64 size = g_paste_item_get_size (item);

            if (size > priv->biggest_size)
            {
                priv->biggest_uuid = g_paste_item_get_uuid (item);
                priv->biggest_size = size;
            }
        }
    }
}

static void
g_paste_history_private_remove (GPasteHistoryPrivate *priv,
                                GList                *elem,
                                gboolean              remove_leftovers)
{
    if (!elem)
        return;

    GPasteItem *item = elem->data;

    priv->size -= g_paste_item_get_size (item);

    if (remove_leftovers)
    {
        if (_G_PASTE_IS_IMAGE_ITEM (item))
        {
            g_autoptr (GFile) image = g_file_new_for_path (g_paste_item_get_value (item));
            g_file_delete (image,
                           NULL, /* cancellable */
                           NULL); /* error */
        }
        g_object_unref (item);
    }
    priv->history = g_list_delete_link (priv->history, elem);
}

static void
g_paste_history_selected (GPasteHistory *self,
                          GPasteItem    *item)
{
    g_debug ("history: selected");

    g_signal_emit (self,
                   signals[SELECTED],
                   0, /* detail */
                   item,
                   NULL);
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

static void
g_paste_history_update (GPasteHistory     *self,
                        GPasteUpdateAction action,
                        GPasteUpdateTarget target,
                        guint64            position)
{
    g_paste_history_save (self, NULL);

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
g_paste_history_activate_first (GPasteHistory *self,
                                gboolean       select)
{
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    GList *history = priv->history;

    if (!history)
        return;

    GPasteItem *first = history->data;

    priv->size -= g_paste_item_get_size (first);
    g_paste_item_set_state (first, G_PASTE_ITEM_STATE_ACTIVE);
    priv->size += g_paste_item_get_size (first);

    if (select)
        g_paste_history_selected (self, first);
}

static GList *
g_paste_history_private_get_item_by_uuid (const GPasteHistoryPrivate *priv,
                                          const gchar                *uuid,
                                          guint64                    *index)
{
    guint64 idx = 0;

    for (GList *history = priv->history; history; history = history->next, ++idx)
    {
        const GPasteItem *item = history->data;

        if (g_paste_str_equal (g_paste_item_get_uuid (item), uuid))
        {
            if (index)
                *index = idx;
            return history;
        }
    }

    return NULL;
}

static GPasteItem *
g_paste_history_private_get_by_uuid (const GPasteHistoryPrivate *priv,
                                     const gchar                *uuid)
{
    GList *item = g_paste_history_private_get_item_by_uuid (priv, uuid, NULL);

    return (item) ? item->data : NULL;
}

static void
g_paste_history_private_check_memory_usage (GPasteHistoryPrivate *priv)
{
    guint64 max_memory = g_paste_settings_get_max_memory_usage (priv->settings) * 1024 * 1024;

    while (priv->size > max_memory && priv->biggest_uuid)
    {
        GList *biggest = g_paste_history_private_get_item_by_uuid (priv, priv->biggest_uuid, NULL);

        g_return_if_fail (biggest);

        g_paste_history_private_remove (priv, biggest, TRUE);
        g_paste_history_private_elect_new_biggest (priv);
    }
}

static void
g_paste_history_private_check_size (GPasteHistoryPrivate *priv)
{
    GList *history = priv->history;
    guint64 max_history_size = g_paste_settings_get_max_history_size (priv->settings);
    guint64 length = g_list_length (history);

    if (length > max_history_size)
    {
        history = g_list_nth (history, max_history_size);
        g_return_if_fail (history);
        history->prev->next = NULL;
        history->prev = NULL;

        for (GList *_history = history; _history; _history = g_list_next (_history))
            priv->size -= g_paste_item_get_size (_history->data);
        g_list_free_full (history,
                          g_object_unref);
    }
}

static gboolean
g_paste_history_private_is_growing_line (GPasteHistoryPrivate *priv,
                                         GPasteItem           *old,
                                         GPasteItem           *new)
{
    if (_G_PASTE_IS_IMAGE_ITEM (old) && _G_PASTE_IS_IMAGE_ITEM (new))
        return g_paste_image_item_is_growing (_G_PASTE_IMAGE_ITEM (new), _G_PASTE_IMAGE_ITEM (old));

    if (!(g_paste_settings_get_growing_lines (priv->settings) &&
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

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    guint64 max_memory = g_paste_settings_get_max_memory_usage (priv->settings) * 1024 * 1024;

    if (g_paste_item_get_size (item) > max_memory)
        return;

    GList *history = priv->history;
    gboolean election_needed = FALSE;
    GPasteUpdateTarget target = G_PASTE_UPDATE_TARGET_ALL;

    g_debug ("history: add");

    if (history)
    {
        GPasteItem *old_first = history->data;

        if (g_paste_item_equals (old_first, item))
            return;

        if (new_selection && g_paste_history_private_is_growing_line (priv, old_first, item))
        {
            target = G_PASTE_UPDATE_TARGET_POSITION;
            g_paste_history_private_remove (priv, history, FALSE);
        }
        else
        {
            /* size may change when state is idle */
            priv->size -= g_paste_item_get_size (old_first);
            g_paste_item_set_state (old_first, G_PASTE_ITEM_STATE_IDLE);

            guint64 size = g_paste_item_get_size (old_first);

            priv->size += size;

            if (size >= priv->biggest_size)
            {
                priv->biggest_uuid = g_paste_item_get_uuid (old_first);
                priv->biggest_size = size;
            }

            for (history = history->next; history; history = history->next)
            {
                if (g_paste_item_equals (history->data, item) || (new_selection && g_paste_history_private_is_growing_line (priv, history->data, item)))
                {
                    if (g_paste_str_equal (priv->biggest_uuid, g_paste_item_get_uuid (history->data)))
                        election_needed = TRUE;
                    g_paste_history_private_remove (priv, history, FALSE);
                    break;
                }
            }
        }
    }

    priv->history = g_list_prepend (priv->history, item);

    g_paste_history_activate_first (self, FALSE);
    priv->size += g_paste_item_get_size (item);

    g_paste_history_private_check_size (priv);

    if (election_needed)
        g_paste_history_private_elect_new_biggest (priv);

    g_paste_history_private_check_memory_usage (priv);
    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, target, 0);
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

    _g_paste_history_add (self, item, TRUE);
}

static void
g_paste_history_remove_common (GPasteHistory        *self,
                               GPasteHistoryPrivate *priv,
                               GList                *item,
                               guint64               index)
{
    if (!item)
        return;

    gboolean was_biggest = g_paste_str_equal (priv->biggest_uuid, g_paste_item_get_uuid (item->data));

    g_paste_history_private_remove (priv, item, TRUE);

    if (!index)
        g_paste_history_activate_first (self, TRUE);

    if (was_biggest)
        g_paste_history_private_elect_new_biggest (priv);

    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REMOVE, G_PASTE_UPDATE_TARGET_POSITION, index);
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

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    GList *history = priv->history;

    g_debug ("history: remove '%" G_GUINT64_FORMAT "'", index);

    if (index >= g_list_length (history))
        return;

    GList *item = g_list_nth (history, index);

    g_paste_history_remove_common (self, priv, item, index);
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

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    g_debug ("history: remove '%s", uuid);

    guint64 index;
    GList *item = g_paste_history_private_get_item_by_uuid (priv, uuid, &index);

    if (!item)
        return FALSE;

    g_paste_history_remove_common (self, priv, item, index);
    return TRUE;
}
/**
 * g_paste_history_refresh_item_size:
 * @self: a #GPasteHistory instance
 * @item: the #GPasteItem to refresh
 * @old_size: the former size of the item
 *
 * Refresh the cached size of the #GPasteItem
 */
G_PASTE_VISIBLE void
g_paste_history_refresh_item_size (GPasteHistory    *self,
                                   const GPasteItem *item,
                                   guint64           old_size)
{
    g_return_if_fail (_G_PASTE_IS_HISTORY (self));
    g_return_if_fail (_G_PASTE_IS_ITEM (item));

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    if (!g_list_find (priv->history, item))
        return;

    guint64 size = g_paste_item_get_size (item);

    g_return_if_fail (old_size <= size);

    priv->size += (size - old_size);

    if (size > priv->biggest_size)
        g_paste_history_private_elect_new_biggest (priv);

    g_paste_history_private_check_memory_usage (priv);
}

static GPasteItem *
g_paste_history_private_get (const GPasteHistoryPrivate *priv,
                             guint64                     index)
{
    GList *history = priv->history;

    if (index >= g_list_length (history))
        return NULL;

    return G_PASTE_ITEM (g_list_nth_data (history, index));
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

    return g_paste_history_private_get (_g_paste_history_get_instance_private (self), index);
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

    return g_paste_history_private_get_by_uuid (_g_paste_history_get_instance_private (self), uuid);
}

/**
 * g_paste_history_dup:
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteItem
 *
 * Get a #GPasteItem from the #GPasteHistory
 * free it with g_object_unref
 *
 * Returns: (transfer full): a #GPasteItem
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_history_dup (GPasteHistory *self,
                     guint64        index)
{
    g_return_val_if_fail (_G_PASTE_IS_HISTORY (self), NULL);

    return g_object_ref (g_paste_history_private_get (_g_paste_history_get_instance_private (self), index));
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

    GPasteItem *item = g_paste_history_private_get_by_uuid (_g_paste_history_get_instance_private (self), uuid);

    if (!item)
        return FALSE;

    _g_paste_history_add (self, item, FALSE);
    g_paste_history_selected (self, item);
    return TRUE;
}

static void
_g_paste_history_replace (GPasteHistory *self,
                          guint64        index,
                          GPasteItem    *new,
                          GList         *todel)
{
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    GPasteItem *old = todel->data;
    gboolean was_biggest = g_paste_str_equal (priv->biggest_uuid, g_paste_item_get_uuid (old));

    priv->size -= g_paste_item_get_size (old);
    priv->size += g_paste_item_get_size (new);

    g_object_unref (old);
    todel->data = new;

    if (was_biggest)
        g_paste_history_private_elect_new_biggest (priv);

    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_POSITION, index);
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

    const GPasteHistoryPrivate *priv = _g_paste_history_get_instance_private (self);
    guint64 index;
    GList *todel = g_paste_history_private_get_item_by_uuid (priv, uuid, &index);

    if (!todel)
        return;

    GPasteItem *item = todel->data;

    g_return_if_fail (_G_PASTE_IS_TEXT_ITEM (item) && g_paste_str_equal (g_paste_item_get_kind (item), "Text"));

    GPasteItem *new = g_paste_text_item_new (contents);

    _g_paste_history_replace (self, index, new, todel);

    if (!index)
        g_paste_history_selected (self, new);
}

static GPasteItem *
_g_paste_history_private_get_password (const GPasteHistoryPrivate *priv,
                                       const gchar                *name,
                                       guint64                    *index)
{
    guint64 idx = 0;

    for (GList *h = priv->history; h; h = g_list_next (h), ++idx)
    {
        GPasteItem *i = h->data;
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

    const GPasteHistoryPrivate *priv = _g_paste_history_get_instance_private (self);
    guint64 index;
    GList *todel = g_paste_history_private_get_item_by_uuid (priv, uuid, &index);

    g_return_if_fail (todel);

    GPasteItem *item = todel->data;

    g_return_if_fail (_G_PASTE_IS_TEXT_ITEM (item) && g_paste_str_equal (g_paste_item_get_kind (item), "Text"));
    g_return_if_fail (!_g_paste_history_private_get_password (priv, name, NULL));

    GPasteItem *password = g_paste_password_item_new (name, g_paste_item_get_real_value (item));

    _g_paste_history_replace (self, index, password, todel);
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

    const GPasteHistoryPrivate *priv = _g_paste_history_get_instance_private (self);
    GPasteItem *item = _g_paste_history_private_get_password (priv, name, NULL);

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

    const GPasteHistoryPrivate *priv = _g_paste_history_get_instance_private (self);
    guint64 index;

    if (_g_paste_history_private_get_password (priv, name, &index))
        g_paste_history_remove (self, index);
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

    const GPasteHistoryPrivate *priv = _g_paste_history_get_instance_private (self);
    guint64 index = 0;
    GPasteItem *item = _g_paste_history_private_get_password (priv, old_name, &index);
    if (item)
    {
        g_paste_password_item_set_name (G_PASTE_PASSWORD_ITEM (item), new_name);
        g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_POSITION, index);
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

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    g_list_free_full (priv->history, g_object_unref);
    priv->history = NULL;
    priv->size = 0;

    g_paste_history_private_elect_new_biggest (priv);
    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REMOVE, G_PASTE_UPDATE_TARGET_ALL, 0);
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

    const GPasteHistoryPrivate *priv = _g_paste_history_get_instance_private (self);

    g_paste_storage_backend_write_history (priv->backend, (name) ? name : priv->name, priv->history);
}

/**
 * g_paste_history_load:
 * @self: a #GPasteHistory instance
 * @name: (nullable): the name of the history to load, defaults to the configured one
 *
 * Load the #GPasteHistory from the history file
 */
G_PASTE_VISIBLE void
g_paste_history_load (GPasteHistory *self,
                      const gchar   *name)
{
    g_return_if_fail (_G_PASTE_IS_HISTORY (self));
    g_return_if_fail (!name || g_utf8_validate (name, -1, NULL));

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    if (priv->name && g_paste_str_equal(name, priv->name))
        return;

    g_list_free_full (priv->history,
                      g_object_unref);
    priv->history = NULL;
    priv->size = 0;

    g_free (priv->name);
    priv->name = g_strdup ((name) ? name : g_paste_settings_get_history_name (priv->settings));

    g_paste_storage_backend_read_history (priv->backend, priv->name, &priv->history, &priv->size);

    if (priv->history)
    {
        g_paste_history_activate_first (self, TRUE);
        g_paste_history_private_elect_new_biggest (priv);
    }
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

    const GPasteHistoryPrivate *priv = _g_paste_history_get_instance_private (self);

    g_paste_settings_set_history_name (priv->settings, name);
}

/**
 * g_paste_history_delete:
 * @self: a #GPasteHistory instance
 * @name: (nullable): the history to delete (defaults to the configured one)
 * @error: a #GError
 *
 * Delete the current #GPasteHistory
 */
G_PASTE_VISIBLE void
g_paste_history_delete (GPasteHistory *self,
                        const gchar   *name,
                        GError       **error)
{
    g_return_if_fail (_G_PASTE_IS_HISTORY (self));

    const GPasteHistoryPrivate *priv = _g_paste_history_get_instance_private (self);

    g_autoptr (GFile) history_file = g_paste_util_get_history_file ((name) ? name : priv->name, "xml");

    if (g_paste_str_equal (name, priv->name))
        g_paste_history_empty (self);

    if (g_file_query_exists (history_file,
                             NULL)) /* cancellable */
    {
        g_file_delete (history_file,
                       NULL, /* cancellable */
                       error);
    }
}

static void
g_paste_history_history_name_changed (GPasteHistory *self)
{
    const GPasteHistoryPrivate *priv = _g_paste_history_get_instance_private (self);

    g_debug ("history: name changed to '%s'", priv->name);

    g_paste_history_load (self, NULL);
    g_paste_history_emit_switch (self, priv->name);
    g_paste_history_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0);
}

static void
g_paste_history_settings_changed (GPasteSettings *settings G_GNUC_UNUSED,
                                  const gchar    *key,
                                  gpointer        user_data)
{
    GPasteHistory *self = user_data;
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    /* FIXME: track text item size settings */
    if (g_paste_str_equal(key, G_PASTE_MAX_HISTORY_SIZE_SETTING))
        g_paste_history_private_check_size (priv);
    else if (g_paste_str_equal (key, G_PASTE_MAX_MEMORY_USAGE_SETTING))
        g_paste_history_private_check_memory_usage (priv);
    else if (g_paste_str_equal (key, G_PASTE_HISTORY_NAME_SETTING))
        g_paste_history_history_name_changed (self);
}

static void
g_paste_history_dispose (GObject *object)
{
    GPasteHistory *self = G_PASTE_HISTORY (object);
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    GPasteSettings *settings = priv->settings;

    g_clear_object (&priv->backend);

    if (settings)
    {
        g_signal_handler_disconnect (settings, priv->c_signals[C_CHANGED]);
        g_clear_object (&priv->settings);
    }

    G_OBJECT_CLASS (g_paste_history_parent_class)->dispose (object);
}

static void
g_paste_history_finalize (GObject *object)
{
    const GPasteHistoryPrivate *priv = _g_paste_history_get_instance_private (G_PASTE_HISTORY (object));

    g_free (priv->name);
    g_list_free_full (priv->history, g_object_unref);

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
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    g_paste_history_private_elect_new_biggest (priv);
}

/**
 * g_paste_history_get_history:
 * @self: a #GPasteHistory instance
 *
 * Get the inner history of a #GPasteHistory
 *
 * Returns: (element-type GPasteItem) (transfer none): The inner history
 */
G_PASTE_VISIBLE const GList *
g_paste_history_get_history (const GPasteHistory *self)
{
    g_return_val_if_fail (_G_PASTE_IS_HISTORY (self), NULL);

    const GPasteHistoryPrivate *priv = _g_paste_history_get_instance_private (self);

    return priv->history;
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
g_paste_history_get_length (const GPasteHistory *self)
{
    g_return_val_if_fail (_G_PASTE_IS_HISTORY (self), 0);

    const GPasteHistoryPrivate *priv = _g_paste_history_get_instance_private (self);

    return g_list_length (priv->history);
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

    const GPasteHistoryPrivate *priv = _g_paste_history_get_instance_private (self);

    return priv->name;
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
g_paste_history_search (const GPasteHistory *self,
                        const gchar         *pattern)
{
    g_return_val_if_fail (_G_PASTE_IS_HISTORY (self), NULL);
    g_return_val_if_fail (pattern && g_utf8_validate (pattern, -1, NULL), NULL);

    g_debug ("history: search '%s'", pattern);

    const GPasteHistoryPrivate *priv = _g_paste_history_get_instance_private (self);
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
    guint64 index = 0;

    for (GList *history = priv->history; history; history = g_list_next (history), ++index)
    {
        const GPasteItem *item = (GPasteItem *) history->data;
        const gchar *uuid = g_paste_item_get_uuid (item);
        gboolean match = FALSE;

        if (g_paste_str_equal (pattern, uuid))
            match = TRUE;
        else if (_G_PASTE_IS_PASSWORD_ITEM (item) && g_paste_str_equal(pattern, g_paste_password_item_get_name (_G_PASTE_PASSWORD_ITEM (item))))
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
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    priv->backend = g_paste_storage_backend_new (G_PASTE_STORAGE_DEFAULT, settings);
    priv->settings = g_object_ref (settings);
    priv->c_signals[C_CHANGED] = g_signal_connect (settings,
                                                   "changed",
                                                   G_CALLBACK (g_paste_history_settings_changed),
                                                   self);

    return self;
}

/**
 * g_paste_history_list:
 * @error: a #GError
 *
 * Get the list of available histories
 *
 * Returns: (transfer full): The list of history names
 *                           free it with g_array_unref
 */
G_PASTE_VISIBLE GStrv
g_paste_history_list (GError **error)
{
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autoptr (GArray) history_names = g_array_new (TRUE, /* zero-terminated */
                                                    TRUE, /* clear */
                                                    sizeof (gchar *));
    g_autoptr (GFile) history_dir = g_paste_util_get_history_dir ();
    g_autoptr (GFileEnumerator) histories = g_file_enumerate_children (history_dir,
                                                                       G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                                                                       G_FILE_QUERY_INFO_NONE,
                                                                       NULL, /* cancellable */
                                                                       error);
    if (error && *error)
        return NULL;

    GFileInfo *history;

    while ((history = g_file_enumerator_next_file (histories,
                                                   NULL, /* cancellable */
                                                   error))) /* error */
    {
        g_autoptr (GFileInfo) h = history;

        if (error && *error)
        {
            g_array_unref (history_names);
            return NULL;
        }

        const gchar *raw_name = g_file_info_get_display_name (h);

        if (g_str_has_suffix (raw_name, ".xml"))
        {
            gchar *name = g_strdup (raw_name);

            name[strlen (name) - 4] = '\0';
            g_array_append_val (history_names, name);
        }
    }

    return g_strdupv ((GStrv) (gpointer) history_names->data);
}
