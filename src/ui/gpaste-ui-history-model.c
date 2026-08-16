// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-ui-history-model.h>

/* One row of the history view.
 *
 * In the positional view the row carries no data of its own: its history index
 * is its position in the model, so there is no second copy to keep in sync. In
 * the search view it carries the @uuid it displays instead.
 *
 * @widget is the row widget currently bound to it, set and cleared by the list
 * view factory; it is what activation and merge-mode picking route through, and
 * it is %NULL whenever the row is not realised. */
struct _GPasteUiHistoryItem
{
    GObject    parent_instance;

    gchar     *uuid;
    GtkWidget *widget; /* borrowed: owned by its GtkListItem */
};

G_PASTE_DEFINE_TYPE (UiHistoryItem, ui_history_item, G_TYPE_OBJECT)

/**
 * g_paste_ui_history_item_get_uuid:
 * @self: a #GPasteUiHistoryItem
 *
 * Get the uuid this row displays, if it is a search result.
 *
 * Returns: (nullable): the uuid, owned by the item, or %NULL for a positional row
 */
const gchar *
g_paste_ui_history_item_get_uuid (GPasteUiHistoryItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_UI_HISTORY_ITEM (self), NULL);

    return self->uuid;
}

/**
 * g_paste_ui_history_item_get_widget:
 * @self: a #GPasteUiHistoryItem
 *
 * Get the row widget currently bound to this item.
 *
 * Returns: (transfer none) (nullable): the widget, or %NULL if the row is not realised
 */
GtkWidget *
g_paste_ui_history_item_get_widget (GPasteUiHistoryItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_UI_HISTORY_ITEM (self), NULL);

    return self->widget;
}

/**
 * g_paste_ui_history_item_set_widget:
 * @self: a #GPasteUiHistoryItem
 * @widget: (nullable): the row widget bound to this item, or %NULL to clear
 *
 * Record which widget currently displays this item.
 */
void
g_paste_ui_history_item_set_widget (GPasteUiHistoryItem *self,
                                    GtkWidget           *widget)
{
    g_return_if_fail (G_PASTE_IS_UI_HISTORY_ITEM (self));

    self->widget = widget;
}

static void
g_paste_ui_history_item_finalize (GObject *object)
{
    GPasteUiHistoryItem *self = G_PASTE_UI_HISTORY_ITEM (object);

    g_free (self->uuid);

    G_OBJECT_CLASS (g_paste_ui_history_item_parent_class)->finalize (object);
}

static void
g_paste_ui_history_item_class_init (GPasteUiHistoryItemClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = g_paste_ui_history_item_finalize;
}

static void
g_paste_ui_history_item_init (GPasteUiHistoryItem *self G_GNUC_UNUSED)
{
}

static GPasteUiHistoryItem *
g_paste_ui_history_item_new (const gchar *uuid)
{
    GPasteUiHistoryItem *self = g_object_new (G_PASTE_TYPE_UI_HISTORY_ITEM, NULL);

    self->uuid = g_strdup (uuid);

    return self;
}

struct _GPasteUiHistoryModel
{
    GObject    parent_instance;

    GPtrArray *items;
    gboolean   by_uuid; /* the rows are named (a search, the favourites) rather than history positions */
};

static void g_paste_ui_history_model_list_model_iface_init (GListModelInterface *iface);

G_PASTE_DEFINE_TYPE_WITH_INTERFACE (UiHistoryModel, ui_history_model, G_TYPE_OBJECT,
                                    G_TYPE_LIST_MODEL, g_paste_ui_history_model_list_model_iface_init)

static GType
g_paste_ui_history_model_get_item_type (GListModel *list G_GNUC_UNUSED)
{
    return G_PASTE_TYPE_UI_HISTORY_ITEM;
}

static guint
g_paste_ui_history_model_get_n_items (GListModel *list)
{
    GPasteUiHistoryModel *self = G_PASTE_UI_HISTORY_MODEL (list);

    return self->items->len;
}

static gpointer
g_paste_ui_history_model_get_item (GListModel *list,
                                   guint       position)
{
    GPasteUiHistoryModel *self = G_PASTE_UI_HISTORY_MODEL (list);

    if (position >= self->items->len)
        return NULL;

    return g_object_ref (g_ptr_array_index (self->items, position));
}

static void
g_paste_ui_history_model_list_model_iface_init (GListModelInterface *iface)
{
    iface->get_item_type = g_paste_ui_history_model_get_item_type;
    iface->get_n_items = g_paste_ui_history_model_get_n_items;
    iface->get_item = g_paste_ui_history_model_get_item;
}

/**
 * g_paste_ui_history_model_peek:
 * @self: a #GPasteUiHistoryModel
 * @position: the position to look at
 *
 * Look at the item at @position without taking a reference, for the callers that
 * only want to reach its bound widget.
 *
 * Returns: (transfer none) (nullable): the item, or %NULL past the end
 */
GPasteUiHistoryItem *
g_paste_ui_history_model_peek (GPasteUiHistoryModel *self,
                               guint64               position)
{
    g_return_val_if_fail (G_PASTE_IS_UI_HISTORY_MODEL (self), NULL);

    if (position >= self->items->len)
        return NULL;

    return g_ptr_array_index (self->items, position);
}

static void
g_paste_ui_history_model_reset (GPasteUiHistoryModel *self,
                                guint64               size,
                                const gchar * const  *uuids)
{
    guint old_len = self->items->len;

    g_ptr_array_set_size (self->items, 0);

    for (guint64 i = 0; i < size; ++i)
        g_ptr_array_add (self->items, g_paste_ui_history_item_new (uuids ? uuids[i] : NULL));

    if (old_len || size)
        g_list_model_items_changed (G_LIST_MODEL (self), 0, old_len, size);
}

/**
 * g_paste_ui_history_model_set_size:
 * @self: a #GPasteUiHistoryModel
 * @size: how many history positions to show
 *
 * Switch to the positional view of @size rows.
 *
 * Growing or shrinking only touches the tail: the rows that stay keep their
 * positions, and whichever of them also changed content is the caller's to
 * report with g_paste_ui_history_model_invalidate ().
 *
 * Returns: %TRUE if every row was rebuilt, leaving nothing to invalidate
 */
gboolean
g_paste_ui_history_model_set_size (GPasteUiHistoryModel *self,
                                   guint64               size)
{
    g_return_val_if_fail (G_PASTE_IS_UI_HISTORY_MODEL (self), FALSE);

    guint old_len = self->items->len;

    /* Coming back from a named view, every row changes role rather than position. */
    if (self->by_uuid)
    {
        self->by_uuid = FALSE;
        g_paste_ui_history_model_reset (self, size, NULL);
        return TRUE;
    }

    if (size == old_len)
        return FALSE;

    if (size > old_len)
    {
        for (guint64 i = old_len; i < size; ++i)
            g_ptr_array_add (self->items, g_paste_ui_history_item_new (NULL));

        g_list_model_items_changed (G_LIST_MODEL (self), old_len, 0, size - old_len);
    }
    else
    {
        g_ptr_array_set_size (self->items, size);

        g_list_model_items_changed (G_LIST_MODEL (self), size, old_len - size, 0);
    }

    return FALSE;
}

/**
 * g_paste_ui_history_model_set_uuids:
 * @self: a #GPasteUiHistoryModel
 * @uuids: (array zero-terminated=1) (nullable): the uuids to list
 *
 * Switch to a uuid-driven view listing @uuids, which is what both a search and
 * the favourites filter show: rows named rather than counted.
 */
void
g_paste_ui_history_model_set_uuids (GPasteUiHistoryModel *self,
                                     const gchar * const  *uuids)
{
    g_return_if_fail (G_PASTE_IS_UI_HISTORY_MODEL (self));

    self->by_uuid = TRUE;

    g_paste_ui_history_model_reset (self, (uuids) ? g_strv_length ((GStrv) uuids) : 0, uuids);
}

/**
 * g_paste_ui_history_model_invalidate:
 * @self: a #GPasteUiHistoryModel
 * @position: the first row to invalidate
 * @n_items: how many rows to invalidate
 *
 * Report that @n_items rows from @position still exist at those positions but no
 * longer show the same thing — history entries shift when one is added or
 * removed. The bound rows refetch; the rest are none of the view's business.
 */
void
g_paste_ui_history_model_invalidate (GPasteUiHistoryModel *self,
                                     guint64               position,
                                     guint64               n_items)
{
    g_return_if_fail (G_PASTE_IS_UI_HISTORY_MODEL (self));

    if (!n_items || position + n_items > self->items->len)
        return;

    /* Fresh objects for the range, not just the signal. #GtkListView pairs each
     * released row widget with the item pointer it was bound to, so being handed
     * the same pointers back it reuses each widget for its own item and runs
     * neither unbind nor bind — the rows would keep showing what they showed.
     * Replacing the items is what makes "same position, different content"
     * visible to it. */
    for (guint64 i = position; i < position + n_items; ++i)
    {
        GPasteUiHistoryItem *old = g_ptr_array_index (self->items, i);

        /* A search row is identified by its uuid, which the invalidation does
         * not change; a positional one carries none. */
        g_ptr_array_index (self->items, i) = g_paste_ui_history_item_new (g_paste_ui_history_item_get_uuid (old));

        g_object_unref (old);
    }

    g_list_model_items_changed (G_LIST_MODEL (self), position, n_items, n_items);
}

/**
 * g_paste_ui_history_model_item_replaced:
 * @self: a #GPasteUiHistoryModel
 * @position: the position that changed
 *
 * Report that a single row changed in place, with nothing around it moving.
 */
void
g_paste_ui_history_model_item_replaced (GPasteUiHistoryModel *self,
                                        guint64               position)
{
    g_paste_ui_history_model_invalidate (self, position, 1);
}

/**
 * g_paste_ui_history_model_item_replaced_by_uuid:
 * @self: a #GPasteUiHistoryModel
 * @uuid: the uuid of the item that changed
 *
 * Report that the row showing @uuid no longer shows the same thing.
 *
 * The counterpart of g_paste_ui_history_model_item_replaced() for a uuid-driven
 * view, where a position means nothing. A uuid this view does not list is not an
 * error: an update is broadcast to every client whatever each is showing.
 */
void
g_paste_ui_history_model_item_replaced_by_uuid (GPasteUiHistoryModel *self,
                                                const gchar          *uuid)
{
    g_return_if_fail (G_PASTE_IS_UI_HISTORY_MODEL (self));

    if (!uuid || !*uuid)
        return;

    for (guint i = 0; i < self->items->len; ++i)
    {
        if (g_paste_str_equal (g_paste_ui_history_item_get_uuid (g_ptr_array_index (self->items, i)), uuid))
        {
            g_paste_ui_history_model_invalidate (self, i, 1);
            return;
        }
    }
}

static void
g_paste_ui_history_model_dispose (GObject *object)
{
    GPasteUiHistoryModel *self = G_PASTE_UI_HISTORY_MODEL (object);

    g_clear_pointer (&self->items, g_ptr_array_unref);

    G_OBJECT_CLASS (g_paste_ui_history_model_parent_class)->dispose (object);
}

static void
g_paste_ui_history_model_class_init (GPasteUiHistoryModelClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_history_model_dispose;
}

static void
g_paste_ui_history_model_init (GPasteUiHistoryModel *self)
{
    self->items = g_ptr_array_new_with_free_func (g_object_unref);
}

/**
 * g_paste_ui_history_model_new:
 *
 * Create a new #GPasteUiHistoryModel, initially empty.
 *
 * Returns: (transfer full): a newly allocated #GPasteUiHistoryModel
 *          free it with g_object_unref
 */
GPasteUiHistoryModel *
g_paste_ui_history_model_new (void)
{
    return g_object_new (G_PASTE_TYPE_UI_HISTORY_MODEL, NULL);
}
