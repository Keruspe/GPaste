// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-gsettings-keys.h>
#include <gpaste-3/gpaste-update-enums.h>

#include <adwaita.h>

#include <gpaste-ui-history.h>
#include <gpaste-ui-history-model.h>
#include <gpaste-ui-item.h>

struct _GPasteUiHistory
{
    GtkBox parent_instance;

    GPasteClient   *client;
    GPasteSettings *settings;
    GPasteUiPanel  *panel;

    AdwStatusPage        *status_page;
    GtkScrolledWindow    *scroll;
    GtkListView          *list_view;
    GPasteUiHistoryModel *model;
    GtkSelectionModel    *selection_model; /* borrowed: the list view owns it */

    GtkWindow      *rootwin;

    guint64         size;       /* number of rows currently in the model */
    guint64         limit;      /* how many items we currently allow on screen; grows lazily */
    guint64         available;  /* last known total size of the history */
    gboolean        loading;    /* a lazy-growth refresh is in flight */
    guint64         display_generation; /* bumped per refresh and per search; stale callbacks bail */
    gboolean        selection_mode; /* merge mode: rows are multi-selectable */
    GArray         *selection;       /* selected positions, in the order they were picked */
    gint32          item_height;

    gchar          *search;
};

enum
{
    SELECTION_CHANGED,

    LAST_SIGNAL
};

static guint64 signals[LAST_SIGNAL] = { 0 };

enum
{
    PROP_0,
    PROP_SELECTION_MODE,

    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

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

/* In merge mode a plain click should toggle that one row, where the list view
 * would otherwise replace the whole selection, so claim the press before its own
 * gesture runs. The gesture lives on the row widget, which its #GtkListItem owns
 * and outlives, so the list item can be borrowed to ask where the row sits. */
static void
on_row_pressed (GtkGestureClick *gesture,
                gint             n_press G_GNUC_UNUSED,
                gdouble          x       G_GNUC_UNUSED,
                gdouble          y       G_GNUC_UNUSED,
                gpointer         user_data)
{
    GPasteUiHistory *self = user_data;

    if (!self->selection_mode || !self->selection_model)
        return;

    GtkListItem *list_item = g_object_get_data (G_OBJECT (gesture), "gpaste-list-item");
    guint position = gtk_list_item_get_position (list_item);

    if (position == GTK_INVALID_LIST_POSITION)
        return;

    gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);

    if (gtk_selection_model_is_selected (self->selection_model, position))
        gtk_selection_model_unselect_item (self->selection_model, position);
    else
        gtk_selection_model_select_item (self->selection_model, position, FALSE);
}

/* The list view recycles a small pool of row widgets: "setup" builds one empty
 * row, "bind" points it at the model item it should display (which kicks off the
 * async fetch of its content), and "unbind" clears it so a fetch from the
 * previous binding cannot land on the reused widget. */
static void
g_paste_ui_history_setup_item (GtkListItemFactory *factory G_GNUC_UNUSED,
                               GtkListItem        *list_item,
                               gpointer            user_data)
{
    GPasteUiHistory *self = user_data;
    GtkWidget *item = g_paste_ui_item_new (self->client, self->settings, self->rootwin, (guint64) -1);
    GtkGesture *gesture = gtk_gesture_click_new ();

    gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (gesture), GTK_PHASE_CAPTURE);
    g_object_set_data (G_OBJECT (gesture), "gpaste-list-item", list_item);
    g_signal_connect_object (gesture, "pressed", G_CALLBACK (on_row_pressed), self, 0);
    gtk_widget_add_controller (item, GTK_EVENT_CONTROLLER (gesture));

    gtk_list_item_set_child (list_item, item);
}

static void
g_paste_ui_history_bind_item (GtkListItemFactory *factory G_GNUC_UNUSED,
                              GtkListItem        *list_item,
                              gpointer            user_data G_GNUC_UNUSED)
{
    GPasteUiHistoryItem *model_item = gtk_list_item_get_item (list_item);
    GtkWidget *widget = gtk_list_item_get_child (list_item);
    const gchar *uuid = g_paste_ui_history_item_get_uuid (model_item);

    g_paste_ui_history_item_set_widget (model_item, widget);

    /* A search row knows its uuid; a positional one's history index is simply
     * where it sits, so there is no second copy of it to go stale. */
    if (uuid)
        g_paste_ui_item_set_uuid (G_PASTE_UI_ITEM (widget), uuid);
    else
        g_paste_ui_item_set_index (G_PASTE_UI_ITEM (widget), gtk_list_item_get_position (list_item));
}

static void
g_paste_ui_history_unbind_item (GtkListItemFactory *factory G_GNUC_UNUSED,
                                GtkListItem        *list_item,
                                gpointer            user_data G_GNUC_UNUSED)
{
    GPasteUiHistoryItem *model_item = gtk_list_item_get_item (list_item);
    GtkWidget *widget = gtk_list_item_get_child (list_item);

    if (model_item)
        g_paste_ui_history_item_set_widget (model_item, NULL);

    /* Clears the row, and invalidates whatever this binding left in flight. */
    g_paste_ui_item_set_index (G_PASTE_UI_ITEM (widget), (guint64) -1);
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

    g_autoptr (GError) error = NULL;
    guint64 old_size = self->size;
    guint64 new_size = g_paste_client_get_history_size_finish (self->client, res, &error);

    self->loading = FALSE;

    /* A failed size reads back as 0, which would blank the list and tell the
     * panel the history is empty. Keep showing what is there instead. */
    if (error)
    {
        g_warning ("Could not get the size of history \"%s\": %s", cdata->name, error->message);
        return;
    }

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

    gboolean rebuilt = g_paste_ui_history_model_set_size (self->model, self->size);

    /* The rows the resize just added or dropped are already accounted for. The
     * ones that survive it keep their positions but not necessarily their
     * contents — history entries shift as one is added or removed — so those
     * from @from_index on have to refetch. */
    guint64 kept = MIN (old_size, self->size);

    if (!rebuilt && cdata->from_index < kept)
        g_paste_ui_history_model_invalidate (self->model, cdata->from_index, kept - cdata->from_index);
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

    g_autoptr (GError) error = NULL;

    cdata->name = g_paste_client_get_history_name_finish (self->client, res, &error);

    /* No name, no history to size: the daemon went away mid-refresh (a re-exec
     * after an upgrade is the usual way). Passing the %NULL on would build the
     * next call as g_variant_new ("(s)", NULL). */
    if (!cdata->name)
    {
        g_warning ("Could not get the history name: %s", error->message);
        /* This refresh is still the current one (checked above), so it owns the
         * flag: leaving it set would wedge lazy growth for good. */
        self->loading = FALSE;
        display_callback_data_free (cdata);
        return;
    }

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
    gdouble upper = gtk_adjustment_get_upper (adjustment);

    /* The list view owns the rows, so ask the laid-out content how tall one is
     * rather than measuring a widget we do not hold: upper covers every row in
     * the model, extrapolated from the ones actually realised. */
    if (upper > 0 && self->size)
        self->item_height = MAX (1, (gint32) (upper / self->size));

    /* Cap eager filling assuming a sane minimum row height, so zero/near-zero
     * height rows can't keep upper <= page forever and load the whole history. */
    guint64 max_fill = (guint64) (page / G_PASTE_UI_HISTORY_MIN_ROW_HEIGHT) + 2;

    /* page <= 0 means the viewport is not allocated yet. */
    if (page > 0 && upper <= page &&
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

    g_auto (GStrv) results = g_paste_client_search_finish (self->client, res, NULL /* error */);
    /* A failed search leaves this NULL, and g_strv_length() has no more patience
     * with that than the daemon side had (c0022cf2). */
    self->size = (results) ? g_strv_length (results) : 0;

    /* No lazy growth to stage here: the view realises only the rows on screen,
     * so every match can be listed and each fetches its own content on demand. */
    g_paste_ui_history_model_set_search (self->model, (const gchar * const *) results);

    if (self->size)
        g_paste_ui_history_show_list (self);
    else
        g_paste_ui_history_show_status (self, "edit-find-symbolic", _("No Results"));
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
        g_paste_ui_history_refresh (self, 0);
    }
    else
    {
        g_set_str (&self->search, search);
        g_paste_client_search (self->client, search, on_search_ready, display_callback_data_new (self, 0));
    }
}

static void
g_paste_ui_history_select_uuid (GPasteUiHistory *self,
                                const gchar     *uuid)
{
    g_paste_client_select (self->client, uuid, NULL, NULL);

    if (g_paste_settings_get_close_on_select (self->settings))
        gtk_window_close (self->rootwin); /* Exit the application */
}

static void
on_activate_element_ready (GObject      *source_object G_GNUC_UNUSED,
                           GAsyncResult *res,
                           gpointer      user_data)
{
    g_autoptr (GPasteUiHistory) self = user_data;

    if (!self->client)
        return;

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClientItem) item = g_paste_client_get_element_at_index_finish (self->client, res, &error);

    /* The activation simply does not happen otherwise, which is worth a word:
     * the user pressed a row and nothing at all moved. */
    if (!item)
    {
        g_warning ("Could not get the item to activate: %s", error->message);
        return;
    }

    g_paste_ui_history_select_uuid (self, g_paste_client_item_get_uuid (item));
}

/* Rows are only realised while they are on screen, so the item to activate may
 * have no widget to ask: for a search row the model still holds its uuid, and a
 * positional one is at the index it sits at, which is a round trip away. */
static gboolean
g_paste_ui_history_activate_position (GPasteUiHistory *self,
                                      guint64          position)
{
    if (!self->client)
        return FALSE;

    GPasteUiHistoryItem *item = g_paste_ui_history_model_peek (self->model, position);

    if (!item)
        return FALSE;

    GtkWidget *widget = g_paste_ui_history_item_get_widget (item);

    if (widget)
        return g_paste_ui_item_activate (G_PASTE_UI_ITEM (widget));

    const gchar *uuid = g_paste_ui_history_item_get_uuid (item);

    if (uuid)
        g_paste_ui_history_select_uuid (self, uuid);
    else
        g_paste_client_get_element_at_index (self->client, position, on_activate_element_ready, g_object_ref (self));

    return TRUE;
}

static void
on_item_activated (GtkListView *list_view G_GNUC_UNUSED,
                   guint        position,
                   gpointer     user_data)
{
    g_paste_ui_history_activate_position (user_data, position);
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

    return g_paste_ui_history_activate_position (self, 0);
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

    return g_paste_ui_history_activate_position (self, index);
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
            /* Positions mean history indices, which the search view does not
             * list; and lazy loading means the changed one may be past the rows
             * we hold at all (a password renamed deep in the history, say). The
             * model shrugs off both: it only reports what it has. */
            if (!self->search)
                g_paste_ui_history_model_item_replaced (self->model, position);
            break;
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

/* Where @position sits in the pick order, or -1 when it is not picked. */
static gint
g_paste_ui_history_picked_at (GPasteUiHistory *self,
                              guint            position)
{
    for (guint i = 0; i < self->selection->len; ++i)
    {
        if (g_array_index (self->selection, guint, i) == position)
            return (gint) i;
    }

    return -1;
}

/* The selection model tracks which positions are picked; the merge wants them in
 * the order they were picked, which a #GtkBitset cannot express. So mirror each
 * change into @selection, which keeps that order.
 *
 * Positions, not uuids: a row that is not realised has no widget to read a uuid
 * off, and select-all over a lazily loaded history picks plenty of those. They
 * are resolved in one go by g_paste_ui_history_get_selected_uuids (). */
static void
on_selection_changed (GtkSelectionModel *model,
                      guint              position,
                      guint              n_items,
                      gpointer           user_data)
{
    GPasteUiHistory *self = user_data;

    for (guint i = position; i < position + n_items; ++i)
    {
        gint at = g_paste_ui_history_picked_at (self, i);

        if (gtk_selection_model_is_selected (model, i))
        {
            if (at < 0)
                g_array_append_val (self->selection, i);
        }
        else if (at >= 0)
            g_array_remove_index (self->selection, at);
    }

    g_signal_emit (self, signals[SELECTION_CHANGED], 0, self->selection->len);
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

    gboolean changed = (self->selection_mode != selection_mode);

    self->selection_mode = selection_mode;
    g_array_set_size (self->selection, 0);

    /* Selectability is the selection model's to grant, so swap the model rather
     * than the rows: a #GtkMultiSelection to pick with, a #GtkNoSelection the
     * rest of the time. Whichever is installed, the list view owns it. */
    GListModel *model = G_LIST_MODEL (g_object_ref (self->model));
    g_autoptr (GtkSelectionModel) selection = (selection_mode)
        ? GTK_SELECTION_MODEL (gtk_multi_selection_new (model))
        : GTK_SELECTION_MODEL (gtk_no_selection_new (model));

    g_signal_connect_object (selection, "selection-changed", G_CALLBACK (on_selection_changed), self, 0);
    gtk_list_view_set_model (self->list_view, selection);
    self->selection_model = selection;

    /* Clicking picks rather than pastes while picking. */
    gtk_list_view_set_single_click_activate (self->list_view, !selection_mode);

    /* The model that held the old selection is gone with it. */
    g_signal_emit (self, signals[SELECTION_CHANGED], 0, 0u);

    if (changed)
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SELECTION_MODE]);
}

/**
 * g_paste_ui_history_get_selected_uuids:
 * @self: a #GPasteUiHistory instance
 * @length: (out): the number of returned uuids
 *
 * Collect the uuids of the rows selected in merge mode, in the order they were
 * picked (so the merge keeps that order).
 *
 * Search rows carry their uuid; positional ones are the history index they sit
 * at, so those are resolved against the history itself -- one round trip for the
 * whole selection, however much of it was ever on screen.
 *
 * Returns: (transfer full) (nullable): a NULL-terminated array of uuids, or
 *          %NULL if the selection could not be resolved
 */
GStrv
g_paste_ui_history_get_selected_uuids (GPasteUiHistory *self,
                                       guint64         *length)
{
    g_return_val_if_fail (G_PASTE_IS_UI_HISTORY (self), NULL);
    g_return_val_if_fail (length, NULL);

    *length = 0;

    g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
    g_autolist (GPasteClientItem) history = NULL;
    gboolean fetched = FALSE;

    for (guint i = 0; i < self->selection->len; ++i)
    {
        guint position = g_array_index (self->selection, guint, i);
        GPasteUiHistoryItem *item = g_paste_ui_history_model_peek (self->model, position);
        const gchar *uuid = (item) ? g_paste_ui_history_item_get_uuid (item) : NULL;

        if (!uuid)
        {
            /* Positional row: its position is its history index. Fetched once,
             * on the first one that needs it. */
            if (!fetched)
            {
                g_autoptr (GError) error = NULL;

                history = g_paste_client_get_history_sync (self->client, &error);
                fetched = TRUE;

                if (error)
                {
                    g_warning ("Could not read the history to resolve the selection: %s", error->message);
                    return NULL;
                }
            }

            GPasteClientItem *client_item = g_list_nth_data (history, position);

            if (!client_item)
                continue;

            uuid = g_paste_client_item_get_uuid (client_item);
        }

        g_strv_builder_add (builder, uuid);
        ++*length;
    }

    return g_strv_builder_end (builder);
}

static void
g_paste_ui_history_dispose (GObject *object)
{
    GPasteUiHistory *self = G_PASTE_UI_HISTORY (object);

    g_clear_pointer (&self->selection, g_array_unref);

    g_clear_pointer (&self->search, g_free);
    g_clear_object (&self->model);
    g_clear_object (&self->client);
    g_clear_object (&self->settings);

    /* All borrowed: the panel and root window outlive us, and chaining up
     * destroys the children (and with them the selection model). A callback that
     * survives us checks the client above, but g_paste_ui_history_refresh_history()
     * also reaches for these, so leave nothing behind that still looks alive. */
    self->panel = NULL;
    self->rootwin = NULL;
    self->status_page = NULL;
    self->scroll = NULL;
    self->list_view = NULL;
    self->selection_model = NULL;

    G_OBJECT_CLASS (g_paste_ui_history_parent_class)->dispose (object);
}

static void
g_paste_ui_history_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    GPasteUiHistory *self = G_PASTE_UI_HISTORY (object);

    switch (prop_id)
    {
    case PROP_SELECTION_MODE:
        g_value_set_boolean (value, self->selection_mode);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
g_paste_ui_history_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    GPasteUiHistory *self = G_PASTE_UI_HISTORY (object);

    switch (prop_id)
    {
    case PROP_SELECTION_MODE:
        g_paste_ui_history_set_selection_mode (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
g_paste_ui_history_class_init (GPasteUiHistoryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_ui_history_dispose;
    object_class->get_property = g_paste_ui_history_get_property;
    object_class->set_property = g_paste_ui_history_set_property;

    /**
     * GPasteUiHistory:selection-mode:
     *
     * Whether the merge selection mode is on: rows become multi-selectable and
     * stop pasting on click.
     *
     * G_PARAM_EXPLICIT_NOTIFY: the setter does the work and notifies only when
     * the mode actually flipped.
     */
    properties[PROP_SELECTION_MODE] = g_param_spec_boolean ("selection-mode", NULL, NULL, FALSE,
                                                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPERTIES, properties);

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
    self->selection = g_array_new (FALSE, FALSE, sizeof (guint));
    self->model = g_paste_ui_history_model_new ();
}

/**
 * g_paste_ui_history_new:
 * @client: a #GPasteClient instance
 * @settings: a #GPasteSettings instance
 * @panel: the #GPasteUiPanel
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

    GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
    g_signal_connect_object (factory, "setup", G_CALLBACK (g_paste_ui_history_setup_item), self, 0);
    g_signal_connect_object (factory, "bind", G_CALLBACK (g_paste_ui_history_bind_item), self, 0);
    g_signal_connect_object (factory, "unbind", G_CALLBACK (g_paste_ui_history_unbind_item), self, 0);

    /* The model is installed by set_selection_mode() below, which is what picks
     * between selectable rows and plain ones. */
    GtkWidget *list_view = gtk_list_view_new (NULL, factory);
    priv->list_view = GTK_LIST_VIEW (list_view);

    GtkWidget *scroll = gtk_scrolled_window_new ();
    priv->scroll = GTK_SCROLLED_WINDOW (scroll);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand (scroll, TRUE);
    gtk_widget_set_vexpand (scroll, TRUE);
    gtk_widget_set_halign (scroll, GTK_ALIGN_FILL);
    gtk_widget_set_valign (scroll, GTK_ALIGN_FILL);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), list_view);
    gtk_widget_set_visible (scroll, FALSE);
    gtk_box_append (box, scroll);

    GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scroll));
    g_signal_connect_object (vadjustment, "changed", G_CALLBACK (g_paste_ui_history_on_adjustment_changed), self, 0);
    g_signal_connect_object (scroll, "edge-reached", G_CALLBACK (g_paste_ui_history_on_edge_reached), self, 0);

    g_signal_connect_object (list_view, "activate", G_CALLBACK (on_item_activated), self, 0);

    g_paste_ui_history_set_selection_mode (priv, FALSE);

    g_signal_connect_object (client,
                             "update",
                             G_CALLBACK (g_paste_ui_history_on_update),
                             self, 0);

    g_paste_ui_history_on_update (client, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0, self);

    return self;
}
