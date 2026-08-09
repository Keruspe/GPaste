// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-gsettings-keys.h>
#include <gpaste-3/gpaste-update-enums.h>

#include <adwaita.h>

#include <gpaste-ui-history.h>
#include <gpaste-ui-item.h>

struct _GPasteUiHistory
{
    GtkBox parent_instance;

    GPasteClient   *client;
    GPasteSettings *settings;
    GPasteUiPanel  *panel;

    AdwStatusPage      *status_page;
    GtkScrolledWindow  *scroll;
    GtkListBox         *list_box;

    GtkWindow      *rootwin;

    GSList         *items;
    guint64         size;       /* number of item widgets currently allocated */
    guint64         limit;      /* how many items we currently allow on screen; grows lazily */
    guint64         available;  /* last known total size of the history */
    gboolean        loading;    /* a lazy-growth refresh is in flight */
    guint64         display_generation; /* bumped per refresh and per search; stale callbacks bail */
    gboolean        selection_mode; /* merge mode: rows are multi-selectable */
    GPtrArray      *selection;       /* selected uuids, in the order they were picked */
    gint32          item_height;

    gchar          *search;
    GStrv           search_results;
};

enum
{
    SELECTION_CHANGED,

    LAST_SIGNAL
};

static guint64 signals[LAST_SIGNAL] = { 0 };

G_PASTE_DEFINE_TYPE (UiHistory, ui_history, GTK_TYPE_BOX)

static void
g_paste_ui_history_show_status (GPasteUiHistory *self,
                                 const gchar     *icon,
                                 const gchar     *title)
{
    adw_status_page_set_icon_name (self->status_page, icon);
    adw_status_page_set_title (self->status_page, title);
    gtk_widget_set_visible (GTK_WIDGET (self->status_page), TRUE);
    gtk_widget_set_visible (GTK_WIDGET (self->scroll), FALSE);
}

static void
g_paste_ui_history_show_list (GPasteUiHistory *self)
{
    gtk_widget_set_visible (GTK_WIDGET (self->status_page), FALSE);
    gtk_widget_set_visible (GTK_WIDGET (self->scroll), TRUE);
}

static void
on_row_activated (GtkListBox    *history G_GNUC_UNUSED,
                  GtkListBoxRow *row)
{
    g_paste_ui_item_activate (G_PASTE_UI_ITEM (row));
}

static void
g_paste_ui_history_add_item (gpointer data,
                             gpointer user_data)
{
    GtkListBox *list_box = user_data;
    GtkWidget *item = data;

    g_object_ref (item);
    gtk_list_box_append (list_box, item);
}

static void
g_paste_ui_history_add_list (GtkListBox *list_box,
                             GSList     *list)
{
    g_slist_foreach (list, g_paste_ui_history_add_item, list_box);
}

static void
g_paste_ui_history_remove (gpointer data,
                           gpointer user_data)
{
    GtkWidget *item = data;
    GtkListBox *list_box = user_data;

    gtk_list_box_remove (list_box, item);
    g_object_unref (item);
}

static void
g_paste_ui_history_drop_list (GtkListBox *list_box,
                              GSList     *list)
{
    g_slist_foreach (list, g_paste_ui_history_remove, list_box);
    g_slist_free (list);
}

static void g_paste_ui_history_refresh (GPasteUiHistory *self,
                                        guint64          from_index);

/* One batch is a viewport's worth of items: enough to fill the visible area so
 * the list always has something to scroll to while more history remains. Until
 * a row has been measured and the viewport allocated, fall back to a fixed
 * count and let the lazy-growth loop top it up to fill the window. */
#define G_PASTE_UI_HISTORY_DEFAULT_BATCH 20

/* Smallest plausible row height in pixels. Used to bound eager filling: dividing
 * the viewport height by it yields the most rows that could ever be needed, so a
 * run of not-yet-laid-out (zero-height) rows can't keep loading the whole history. */
#define G_PASTE_UI_HISTORY_MIN_ROW_HEIGHT 16.0

static guint64
g_paste_ui_history_batch (GPasteUiHistory *self)
{
    if (self->item_height > 0 && self->scroll)
    {
        GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment (self->scroll);
        gdouble page = gtk_adjustment_get_page_size (vadjustment);

        if (page > 0)
            return (guint64) (page / self->item_height) + 1;
    }

    return G_PASTE_UI_HISTORY_DEFAULT_BATCH;
}

/* Carried by both the refresh and the search callbacks. @self is owned: the
 * widget's only owner is the widget tree, so a window closing mid-flight would
 * otherwise finalize it before the reply lands. @generation is what was current
 * when the call went out — refresh and search write the same rows (indices vs.
 * uuids), so whichever went out last wins and everything older bails. */
typedef struct {
    GPasteUiHistory *self;
    gchar           *name;
    guint64          from_index;
    guint64          generation;
} DisplayCallbackData;

static void
display_callback_data_free (DisplayCallbackData *data)
{
    g_clear_object (&data->self);
    g_free (data->name);
    g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DisplayCallbackData, display_callback_data_free)

static DisplayCallbackData *
display_callback_data_new (GPasteUiHistory *self,
                           guint64          from_index)
{
    DisplayCallbackData *data = g_new0 (DisplayCallbackData, 1);

    data->self = g_object_ref (self);
    data->from_index = from_index;
    data->generation = ++self->display_generation;

    return data;
}

static void
g_paste_ui_history_refresh_history (GObject      *source_object G_GNUC_UNUSED,
                                    GAsyncResult *res,
                                    gpointer      user_data)
{
    g_autoptr (DisplayCallbackData) cdata = user_data;
    GPasteUiHistory *self = cdata->self;

    if (!self->client)
        return;

    /* A later refresh or search superseded this one: leave self->loading set
     * (the newer refresh clears it) and don't re-index from this stale
     * from_index, which could leave shifted rows with wrong indices — or, for a
     * search, overwrite the uuids it just installed. */
    if (cdata->generation != self->display_generation)
        return;

    guint64 old_size = self->size;
    guint64 refreshTextBound = old_size;
    guint64 new_size = g_paste_client_get_history_size_finish (self->client, res, NULL);

    self->loading = FALSE;
    self->available = new_size;
    /* Never keep a display limit larger than what the history can fill: when it
     * shrinks (items removed, emptied, or a smaller history selected), drop back
     * so lazy growth restarts from a single batch instead of eagerly reloading
     * the old depth should the history grow again. */
    self->limit = MIN (self->limit, MAX (g_paste_ui_history_batch (self), new_size));
    self->size = MIN (new_size, self->limit);

    if (self->size)
        g_paste_ui_history_show_list (self);
    else
        g_paste_ui_history_show_status (self, "edit-paste-symbolic", _("Empty"));

    g_paste_ui_panel_update_history_length (self->panel, cdata->name, new_size);

    if (old_size < self->size)
    {
        for (guint64 i = old_size; i < self->size; ++i)
        {
            GtkWidget *item = g_paste_ui_item_new (self->client, self->settings, self->rootwin, i);
            /* Rows loaded while in merge mode must be selectable like the rest. */
            gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (item), self->selection_mode);
            gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (item), !self->selection_mode);
            self->items = g_slist_append (self->items, item);
        }
        g_paste_ui_history_add_list (self->list_box, g_slist_nth (self->items, old_size));
        refreshTextBound = old_size;
    }
    else if (old_size > self->size)
    {
        if (self->size)
        {
            GSList *last = g_slist_nth (self->items, self->size - 1);
            g_return_if_fail (last);
            g_paste_ui_history_drop_list (self->list_box, g_slist_next (last));
            last->next = NULL;
        }
        else
        {
            g_paste_ui_history_drop_list (self->list_box, self->items);
            self->items = NULL;
        }
        refreshTextBound = self->size;
    }

    GSList *item = self->items;

    for (guint64 i = 0; i < cdata->from_index; ++i)
        item = g_slist_next (item);
    for (guint64 i = cdata->from_index; i < refreshTextBound && item; ++i, item = g_slist_next (item))
        g_paste_ui_item_set_index (item->data, i);

    if (!self->item_height && self->items)
        gtk_widget_measure (GTK_WIDGET (self->items->data), GTK_ORIENTATION_VERTICAL, -1, NULL, &self->item_height, NULL, NULL);
}

static void
on_name_ready (GObject      *source_object G_GNUC_UNUSED,
               GAsyncResult *res,
               gpointer      user_data)
{
    DisplayCallbackData *cdata = user_data;
    GPasteUiHistory *self = cdata->self;

    /* Bail before the second round-trip rather than after it: a superseded
     * refresh has nothing left to display. */
    if (!self->client || cdata->generation != self->display_generation)
    {
        display_callback_data_free (cdata);
        return;
    }

    cdata->name = g_paste_client_get_history_name_finish (self->client, res, NULL);

    g_paste_client_get_history_size (self->client, cdata->name, g_paste_ui_history_refresh_history, cdata);
}

static void
g_paste_ui_history_refresh (GPasteUiHistory *self,
                            guint64          from_index)
{
    if (!self->client)
        return;

    if (self->search)
        g_paste_ui_history_search (self, self->search);
    else
    {
        self->loading = TRUE;
        g_paste_client_get_history_name (self->client, on_name_ready, display_callback_data_new (self, from_index));
    }
}

static gboolean
g_paste_ui_history_can_grow (GPasteUiHistory *self)
{
    /* size is MIN (available, limit), so there is more to load iff the history
     * holds more items than our current display limit. */
    return self->client && !self->search && !self->loading && self->available > self->limit;
}

static void
g_paste_ui_history_grow (GPasteUiHistory *self)
{
    self->limit += g_paste_ui_history_batch (self);
    g_paste_ui_history_refresh (self, self->size);
}

/* While the loaded items do not yet overflow the viewport, keep loading more so
 * the view always offers something to scroll to when further items exist. The
 * vertical adjustment emits "changed" when its content or viewport is resized. */
static void
g_paste_ui_history_on_adjustment_changed (GtkAdjustment *adjustment,
                                          gpointer       user_data)
{
    GPasteUiHistory *self = user_data;
    gdouble page = gtk_adjustment_get_page_size (adjustment);

    /* Cap eager filling assuming a sane minimum row height, so zero/near-zero
     * height rows can't keep upper <= page forever and load the whole history. */
    guint64 max_fill = (guint64) (page / G_PASTE_UI_HISTORY_MIN_ROW_HEIGHT) + 2;

    /* page <= 0 means the viewport is not allocated yet. */
    if (page > 0 && gtk_adjustment_get_upper (adjustment) <= page &&
        self->size < max_fill && g_paste_ui_history_can_grow (self))
        g_paste_ui_history_grow (self);
}

/* Once the items overflow the viewport, load another batch each time the user
 * scrolls to the bottom, lazily pulling in the rest of the history on demand. */
static void
g_paste_ui_history_on_edge_reached (GtkScrolledWindow *scroll G_GNUC_UNUSED,
                                    GtkPositionType    pos,
                                    gpointer           user_data)
{
    GPasteUiHistory *self = user_data;

    if (pos == GTK_POS_BOTTOM && g_paste_ui_history_can_grow (self))
        g_paste_ui_history_grow (self);
}

static void
on_search_ready (GObject      *source_object G_GNUC_UNUSED,
                 GAsyncResult *res,
                 gpointer      user_data)
{
    g_autoptr (DisplayCallbackData) cdata = user_data;
    GPasteUiHistory *self = cdata->self;

    /* Only meaningful because cdata holds a ref: dispose clears the client, so a
     * NULL one means we were disposed mid-flight. */
    if (!self->client)
        return;

    /* A newer search or refresh already owns these rows. Without this, typing
     * "a" then "ab" leaves whichever reply happens to land last on screen. */
    if (cdata->generation != self->display_generation)
        return;

    GSList *item = self->items;

    g_set_strv_take (&self->search_results, g_paste_client_search_finish (self->client, res, NULL /* error */));
    /* A failed search leaves this NULL, and g_strv_length() has no more patience
     * with that than the daemon side had (c0022cf2). */
    guint64 search_results_size = (self->search_results) ? g_strv_length (self->search_results) : 0;

    if (search_results_size)
    {
        g_paste_ui_history_show_list (self);

        if (search_results_size > self->size)
            search_results_size = self->size;

        for (guint64 i = 0; i < search_results_size; ++i, item = g_slist_next (item))
            g_paste_ui_item_set_uuid (item->data, self->search_results[i]);
    }
    else
        g_paste_ui_history_show_status (self, "edit-find-symbolic", _("No Results"));

    for (guint64 i = search_results_size; i < self->size; ++i, item = g_slist_next (item))
        g_paste_ui_item_set_index (item->data, (guint64) -1);
}

/**
 * g_paste_ui_history_search:
 * @self: a #GPasteUiHistory instance
 * @search: the search
 *
 * Apply a search to the history list
 */
G_PASTE_VISIBLE void
g_paste_ui_history_search (GPasteUiHistory *self,
                           const gchar     *search)
{
    g_return_if_fail (G_PASTE_IS_UI_HISTORY (self));

    if (!self->client)
        return;

    if (g_paste_str_equal (search, ""))
    {
        g_clear_pointer (&self->search, g_free);
        g_clear_pointer (&self->search_results, g_strfreev);
        g_paste_ui_history_refresh (self, 0);
    }
    else
    {
        g_set_str (&self->search, search);
        g_paste_client_search (self->client, search, on_search_ready, display_callback_data_new (self, 0));
    }
}

/**
 * g_paste_ui_history_select_first:
 * @self: a #GPasteUiHistory instance
 *
 * Select the first element
 *
 * returns: whether anything was selected or not
 */
G_PASTE_VISIBLE gboolean
g_paste_ui_history_select_first (GPasteUiHistory *self)
{
    g_return_val_if_fail (G_PASTE_IS_UI_HISTORY (self), FALSE);

    if (!self->items)
        return FALSE;

    return g_paste_ui_item_activate (self->items->data);
}

/**
 * g_paste_ui_history_activate_index:
 * @self: a #GPasteUiHistory instance
 * @index: the position (0-based) of the item to activate
 *
 * Activate the item currently displayed at @index, if any.
 *
 * returns: whether anything was activated or not
 */
G_PASTE_VISIBLE gboolean
g_paste_ui_history_activate_index (GPasteUiHistory *self,
                                   guint64          index)
{
    g_return_val_if_fail (G_PASTE_IS_UI_HISTORY (self), FALSE);

    GPasteUiItem *item = g_slist_nth_data (self->items, index);

    if (!item)
        return FALSE;

    return g_paste_ui_item_activate (item);
}

static void
g_paste_ui_history_on_update (GPasteClient      *client G_GNUC_UNUSED,
                              GPasteUpdateAction action,
                              GPasteUpdateTarget target,
                              guint64            position,
                              gpointer           user_data)
{
    GPasteUiHistory *self = user_data;
    gboolean refresh = FALSE;

    if (!self->client)
        return;

    switch (target)
    {
    case G_PASTE_UPDATE_TARGET_ALL:
        refresh = TRUE;
        break;
    case G_PASTE_UPDATE_TARGET_POSITION:
        switch (action)
        {
        case G_PASTE_UPDATE_ACTION_REPLACE:
        {
            /* Lazy loading means the changed position may be past the rows we
             * have materialised (a password renamed deep in the history, say);
             * there is simply nothing on screen to refresh then. */
            GPasteUiItem *item = g_slist_nth_data (self->items, position);

            if (item)
                g_paste_ui_item_refresh (item);
            break;
        }
        case G_PASTE_UPDATE_ACTION_REMOVE:
            refresh = TRUE;
            break;
        default:
            g_assert_not_reached ();
        }
        break;
    default:
        g_assert_not_reached ();
    }

    if (refresh)
        g_paste_ui_history_refresh (self, position);
}

static void
on_selected_rows_changed (GtkListBox *list_box G_GNUC_UNUSED,
                          gpointer    user_data)
{
    GPasteUiHistory *self = user_data;

    g_signal_emit (self, signals[SELECTION_CHANGED], 0, self->selection->len);
}

static void
apply_selectable (gpointer data,
                  gpointer user_data)
{
    GtkListBoxRow *row = data;
    const gboolean *on = user_data;

    gtk_list_box_row_set_selectable (row, *on);
    gtk_list_box_row_set_activatable (row, !*on);
}

/**
 * g_paste_ui_history_set_selection_mode:
 * @self: a #GPasteUiHistory instance
 * @selection_mode: whether to enter the multi-selection "merge" mode
 *
 * Toggle the merge selection mode: rows become multi-selectable (and stop
 * activating/pasting on click) so several entries can be picked for merging.
 */
void
g_paste_ui_history_set_selection_mode (GPasteUiHistory *self,
                                       gboolean         selection_mode)
{
    g_return_if_fail (G_PASTE_IS_UI_HISTORY (self));

    self->selection_mode = selection_mode;
    g_ptr_array_set_size (self->selection, 0);

    gtk_list_box_set_selection_mode (self->list_box, selection_mode ? GTK_SELECTION_MULTIPLE : GTK_SELECTION_NONE);
    g_slist_foreach (self->items, apply_selectable, &selection_mode);

    /* Leaving the mode (GTK_SELECTION_NONE) already cleared the selection. */
    g_signal_emit (self, signals[SELECTION_CHANGED], 0, 0u);
}

/**
 * g_paste_ui_history_get_selected_uuids:
 * @self: a #GPasteUiHistory instance
 * @length: (out): the number of returned uuids
 *
 * Collect the uuids of the rows selected in merge mode, in the order they were
 * picked (so the merge keeps that order).
 *
 * Returns: (transfer full): a NULL-terminated array of uuids
 */
GStrv
g_paste_ui_history_get_selected_uuids (GPasteUiHistory *self,
                                       guint64         *length)
{
    g_return_val_if_fail (G_PASTE_IS_UI_HISTORY (self), NULL);
    g_return_val_if_fail (length, NULL);

    g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();

    for (guint i = 0; i < self->selection->len; ++i)
        g_strv_builder_add (builder, g_ptr_array_index (self->selection, i));

    *length = self->selection->len;

    return g_strv_builder_end (builder);
}

/* In merge mode, a plain click should toggle that row's selection (GtkListBox
 * would otherwise replace the whole selection). Claim the press so the default
 * gesture does not run. */
static void
on_row_pressed (GtkGestureClick *gesture,
                gint             n_press G_GNUC_UNUSED,
                gdouble          x       G_GNUC_UNUSED,
                gdouble          y,
                gpointer         user_data)
{
    GPasteUiHistory *self = user_data;

    if (!self->selection_mode)
        return;

    GtkListBoxRow *row = gtk_list_box_get_row_at_y (self->list_box, (gint) y);

    if (!row)
        return;

    gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);

    const gchar *uuid = g_paste_ui_item_get_uuid (G_PASTE_UI_ITEM (row));

    if (gtk_list_box_row_is_selected (row))
    {
        for (guint i = 0; uuid && i < self->selection->len; ++i)
        {
            if (g_paste_str_equal (g_ptr_array_index (self->selection, i), uuid))
            {
                g_ptr_array_remove_index (self->selection, i);
                break;
            }
        }
        gtk_list_box_unselect_row (self->list_box, row);
    }
    else
    {
        if (uuid)
            g_ptr_array_add (self->selection, g_strdup (uuid));
        gtk_list_box_select_row (self->list_box, row);
    }
}

static void
g_paste_ui_history_dispose (GObject *object)
{
    GPasteUiHistory *self = G_PASTE_UI_HISTORY (object);

    g_clear_slist (&self->items, g_object_unref);
    g_clear_pointer (&self->selection, g_ptr_array_unref);

    g_clear_pointer (&self->search, g_free);
    g_clear_pointer (&self->search_results, g_strfreev);
    g_clear_object (&self->client);
    g_clear_object (&self->settings);

    /* All borrowed: the panel and root window outlive us, and chaining up
     * destroys the children. A callback that survives us checks the client
     * above, but g_paste_ui_history_refresh_history() also reaches for these,
     * so leave nothing behind that still looks alive. */
    self->panel = NULL;
    self->rootwin = NULL;
    self->status_page = NULL;
    self->scroll = NULL;
    self->list_box = NULL;

    G_OBJECT_CLASS (g_paste_ui_history_parent_class)->dispose (object);
}

static void
g_paste_ui_history_class_init (GPasteUiHistoryClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_history_dispose;

    signals[SELECTION_CHANGED] = g_signal_new ("selection-changed",
                                               G_PASTE_TYPE_UI_HISTORY,
                                               G_SIGNAL_RUN_LAST,
                                               0, /* class offset */
                                               NULL, /* accumulator */
                                               NULL, /* accumulator data */
                                               g_cclosure_marshal_VOID__UINT,
                                               G_TYPE_NONE,
                                               1,
                                               G_TYPE_UINT);
}

static void
g_paste_ui_history_init (GPasteUiHistory *self)
{
    self->selection = g_ptr_array_new_with_free_func (g_free);
}

/**
 * g_paste_ui_history_new:
 * @client: a #GPasteClient instance
 * @settings: a #GPasteSettings instance
 * @panel: the #GPasteSettingsUiPanel
 * @rootwin: the root #GtkWindow
 *
 * Create a new #GPasteUiHistory for GPaste history
 *
 * Returns: a newly allocated #GPasteUiHistory
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_history_new (GPasteClient   *client,
                        GPasteSettings *settings,
                        GPasteUiPanel  *panel,
                        GtkWindow      *rootwin)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (G_PASTE_IS_UI_PANEL (panel), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = g_object_new (G_PASTE_TYPE_UI_HISTORY,
                                      "orientation", GTK_ORIENTATION_VERTICAL,
                                      NULL);
    GPasteUiHistory *priv = G_PASTE_UI_HISTORY (self);
    GtkBox *box = GTK_BOX (self);

    priv->client = g_object_ref (client);
    priv->settings = g_object_ref (settings);
    priv->panel = panel;
    priv->rootwin = rootwin;
    priv->limit = G_PASTE_UI_HISTORY_DEFAULT_BATCH;

    GtkWidget *status_page = adw_status_page_new ();
    priv->status_page = ADW_STATUS_PAGE (status_page);
    adw_status_page_set_icon_name (priv->status_page, "edit-paste-symbolic");
    adw_status_page_set_title (priv->status_page, _("Empty"));
    gtk_widget_set_hexpand (status_page, TRUE);
    gtk_widget_set_vexpand (status_page, TRUE);
    gtk_box_append (box, status_page);

    GtkWidget *list_box = gtk_list_box_new ();
    priv->list_box = GTK_LIST_BOX (list_box);

    GtkWidget *scroll = gtk_scrolled_window_new ();
    priv->scroll = GTK_SCROLLED_WINDOW (scroll);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand (scroll, TRUE);
    gtk_widget_set_vexpand (scroll, TRUE);
    gtk_widget_set_halign (scroll, GTK_ALIGN_FILL);
    gtk_widget_set_valign (scroll, GTK_ALIGN_FILL);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), list_box);
    gtk_widget_set_visible (scroll, FALSE);
    gtk_box_append (box, scroll);

    GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scroll));
    g_signal_connect_object (vadjustment, "changed", G_CALLBACK (g_paste_ui_history_on_adjustment_changed), self, 0);
    g_signal_connect_object (scroll, "edge-reached", G_CALLBACK (g_paste_ui_history_on_edge_reached), self, 0);

    g_signal_connect (list_box, "row-activated", G_CALLBACK (on_row_activated), NULL);
    g_signal_connect_object (list_box, "selected-rows-changed", G_CALLBACK (on_selected_rows_changed), self, 0);

    /* Toggle-on-click for merge mode; capture phase so it runs before the
     * list box's own selection gesture. */
    GtkGesture *select_gesture = gtk_gesture_click_new ();
    gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (select_gesture), GTK_PHASE_CAPTURE);
    g_signal_connect_object (select_gesture, "pressed", G_CALLBACK (on_row_pressed), self, 0);
    gtk_widget_add_controller (list_box, GTK_EVENT_CONTROLLER (select_gesture));

    g_signal_connect_object (client,
                             "update",
                             G_CALLBACK (g_paste_ui_history_on_update),
                             self, 0);

    g_paste_ui_history_on_update (client, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0, self);

    return self;
}
