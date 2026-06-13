// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-gsettings-keys.h>
#include <gpaste/gpaste-update-enums.h>

#include <adwaita.h>

#include <gpaste-ui-history.h>
#include <gpaste-ui-item.h>

struct _GPasteUiHistory
{
    GtkBox parent_instance;
};

typedef struct
{
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
    gboolean        selection_mode; /* merge mode: rows are multi-selectable */
    GPtrArray      *selection;       /* selected uuids, in the order they were picked */
    gint32          item_height;

    gchar          *search;
    GStrv           search_results;
} GPasteUiHistoryPrivate;

enum
{
    SELECTION_CHANGED,

    LAST_SIGNAL
};

static guint64 signals[LAST_SIGNAL] = { 0 };

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UiHistory, ui_history, GTK_TYPE_BOX)

static void
g_paste_ui_history_show_status (GPasteUiHistory *self,
                                 const gchar     *icon,
                                 const gchar     *title)
{
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    adw_status_page_set_icon_name (priv->status_page, icon);
    adw_status_page_set_title (priv->status_page, title);
    gtk_widget_set_visible (GTK_WIDGET (priv->status_page), TRUE);
    gtk_widget_set_visible (GTK_WIDGET (priv->scroll), FALSE);
}

static void
g_paste_ui_history_show_list (GPasteUiHistory *self)
{
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    gtk_widget_set_visible (GTK_WIDGET (priv->status_page), FALSE);
    gtk_widget_set_visible (GTK_WIDGET (priv->scroll), TRUE);
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

static void
g_paste_ui_history_update_height_request (GPasteSettings *settings,
                                          const gchar    *key G_GNUC_UNUSED,
                                          gpointer        user_data)
{
    GPasteUiHistory *self = user_data;
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);
    guint64 max_displayed = g_paste_settings_get_max_displayed_history_size (settings);

    if (priv->item_height)
        g_object_set (G_OBJECT (priv->list_box), "height-request", max_displayed * priv->item_height, NULL);

    /* max-displayed-history-size is the initial batch and the floor for how many
     * items we keep on screen; lazy scrolling can only ever grow past it. */
    if (priv->limit < max_displayed)
    {
        priv->limit = max_displayed;
        g_paste_ui_history_refresh (self, 0);
    }
}

typedef struct {
    GPasteUiHistory *self;
    gchar           *name;
    guint64          from_index;
} OnUpdateCallbackData;

static void
g_paste_ui_history_refresh_history (GObject      *source_object G_GNUC_UNUSED,
                                    GAsyncResult *res,
                                    gpointer      user_data)
{
    g_autofree OnUpdateCallbackData *cdata = user_data;
    g_autofree gchar *name = cdata->name;
    GPasteUiHistory *self = cdata->self;
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    if (!priv->client)
        return;

    guint64 old_size = priv->size;
    guint64 refreshTextBound = old_size;
    guint64 new_size = g_paste_client_get_history_size_finish (priv->client, res, NULL);
    guint64 max_displayed = g_paste_settings_get_max_displayed_history_size (priv->settings);

    priv->loading = FALSE;
    priv->available = new_size;
    /* Never keep a display limit larger than what the history can fill: when it
     * shrinks (items removed, emptied, or a smaller history selected), drop back
     * so lazy growth restarts from the configured batch instead of eagerly
     * reloading the old depth should the history grow again. */
    priv->limit = MIN (priv->limit, MAX (max_displayed, new_size));
    priv->size = MIN (new_size, priv->limit);

    if (priv->size)
        g_paste_ui_history_show_list (self);
    else
        g_paste_ui_history_show_status (self, "edit-paste-symbolic", _("Empty"));

    g_paste_ui_panel_update_history_length (priv->panel, name, new_size);

    if (old_size < priv->size)
    {
        for (guint64 i = old_size; i < priv->size; ++i)
        {
            GtkWidget *item = g_paste_ui_item_new (priv->client, priv->settings, priv->rootwin, i);
            /* Rows loaded while in merge mode must be selectable like the rest. */
            gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (item), priv->selection_mode);
            gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (item), !priv->selection_mode);
            priv->items = g_slist_append (priv->items, item);
        }
        g_paste_ui_history_add_list (priv->list_box, g_slist_nth (priv->items, old_size));
        refreshTextBound = old_size;
    }
    else if (old_size > priv->size)
    {
        if (priv->size)
        {
            GSList *last = g_slist_nth (priv->items, priv->size - 1);
            g_return_if_fail (last);
            g_paste_ui_history_drop_list (priv->list_box, g_slist_next (last));
            last->next = NULL;
        }
        else
        {
            g_paste_ui_history_drop_list (priv->list_box, priv->items);
            priv->items = NULL;
        }
        refreshTextBound = priv->size;
    }

    GSList *item = priv->items;

    for (guint64 i = 0; i < cdata->from_index; ++i)
        item = g_slist_next (item);
    for (guint64 i = cdata->from_index; i < refreshTextBound && item; ++i, item = g_slist_next (item))
        g_paste_ui_item_set_index (item->data, i);

    if (!priv->item_height && priv->items)
    {
        gtk_widget_measure (GTK_WIDGET (priv->items->data), GTK_ORIENTATION_VERTICAL, -1, NULL, &priv->item_height, NULL, NULL);
        g_paste_ui_history_update_height_request (priv->settings, NULL, self);
    }
}

static void
on_name_ready (GObject      *source_object G_GNUC_UNUSED,
               GAsyncResult *res,
               gpointer      user_data)
{
    OnUpdateCallbackData *cdata = user_data;
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (cdata->self);

    if (!priv->client)
    {
        g_free (user_data);
        return;
    }

    cdata->name = g_paste_client_get_history_name_finish (priv->client, res, NULL);

    g_paste_client_get_history_size (priv->client, cdata->name, g_paste_ui_history_refresh_history, cdata);
}

static void
g_paste_ui_history_refresh (GPasteUiHistory *self,
                            guint64          from_index)
{
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    if (!priv->client)
        return;

    if (priv->search)
        g_paste_ui_history_search (self, priv->search);
    else
    {
        OnUpdateCallbackData *cdata = g_new (OnUpdateCallbackData, 1);
        cdata->self = self;
        cdata->from_index = from_index;

        priv->loading = TRUE;
        g_paste_client_get_history_name (priv->client, on_name_ready, cdata);
    }
}

static gboolean
g_paste_ui_history_can_grow (GPasteUiHistory *self)
{
    const GPasteUiHistoryPrivate *priv = _g_paste_ui_history_get_instance_private (self);

    /* size is MIN (available, limit), so there is more to load iff the history
     * holds more items than our current display limit. */
    return priv->client && !priv->search && !priv->loading && priv->available > priv->limit;
}

static void
g_paste_ui_history_grow (GPasteUiHistory *self)
{
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    priv->limit += g_paste_settings_get_max_displayed_history_size (priv->settings);
    g_paste_ui_history_refresh (self, priv->size);
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

    /* page <= 0 means the viewport is not allocated yet. */
    if (page > 0 && gtk_adjustment_get_upper (adjustment) <= page && g_paste_ui_history_can_grow (self))
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
    GPasteUiHistory *self = user_data;
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    if (!priv->client)
        return;

    GSList *item = priv->items;

    g_clear_pointer (&priv->search_results, g_strfreev);
    priv->search_results = g_paste_client_search_finish (priv->client, res, NULL /* error */);
    guint64 search_results_size = g_strv_length (priv->search_results);

    if (search_results_size)
    {
        g_paste_ui_history_show_list (self);

        if (search_results_size > priv->size)
            search_results_size = priv->size;

        for (guint64 i = 0; i < search_results_size; ++i, item = g_slist_next (item))
            g_paste_ui_item_set_uuid (item->data, priv->search_results[i]);
    }
    else
        g_paste_ui_history_show_status (self, "edit-find-symbolic", _("No Results"));

    for (guint64 i = search_results_size; i < priv->size; ++i, item = g_slist_next (item))
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

    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    if (!priv->client)
        return;

    if (g_paste_str_equal (search, ""))
    {
        g_clear_pointer (&priv->search, g_free);
        g_clear_pointer (&priv->search_results, g_strfreev);
        g_paste_ui_history_refresh (self, 0);
    }
    else
    {
        g_set_str (&priv->search, search);
        g_paste_client_search (priv->client, search, on_search_ready, self);
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

    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    if (!priv->items)
        return FALSE;

    return g_paste_ui_item_activate (priv->items->data);
}

static void
g_paste_ui_history_on_update (GPasteClient      *client G_GNUC_UNUSED,
                              GPasteUpdateAction action,
                              GPasteUpdateTarget target,
                              guint64            position,
                              gpointer           user_data)
{
    GPasteUiHistory *self = user_data;
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);
    gboolean refresh = FALSE;

    if (!priv->client)
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
            g_paste_ui_item_refresh (g_slist_nth_data (priv->items, position));
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

static void
on_selected_rows_changed (GtkListBox *list_box G_GNUC_UNUSED,
                          gpointer    user_data)
{
    GPasteUiHistory *self = user_data;
    const GPasteUiHistoryPrivate *priv = _g_paste_ui_history_get_instance_private (self);

    g_signal_emit (self, signals[SELECTION_CHANGED], 0, priv->selection->len);
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
    g_return_if_fail (_G_PASTE_IS_UI_HISTORY (self));

    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    priv->selection_mode = selection_mode;
    g_ptr_array_set_size (priv->selection, 0);

    gtk_list_box_set_selection_mode (priv->list_box, selection_mode ? GTK_SELECTION_MULTIPLE : GTK_SELECTION_NONE);
    g_slist_foreach (priv->items, apply_selectable, &selection_mode);

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
    g_return_val_if_fail (_G_PASTE_IS_UI_HISTORY (self), NULL);
    g_return_val_if_fail (length, NULL);

    const GPasteUiHistoryPrivate *priv = _g_paste_ui_history_get_instance_private (self);
    g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();

    for (guint i = 0; i < priv->selection->len; ++i)
        g_strv_builder_add (builder, g_ptr_array_index (priv->selection, i));

    *length = priv->selection->len;

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
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    if (!priv->selection_mode)
        return;

    GtkListBoxRow *row = gtk_list_box_get_row_at_y (priv->list_box, (gint) y);

    if (!row)
        return;

    gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);

    const gchar *uuid = g_paste_ui_item_get_uuid (G_PASTE_UI_ITEM (row));

    if (gtk_list_box_row_is_selected (row))
    {
        for (guint i = 0; uuid && i < priv->selection->len; ++i)
        {
            if (g_paste_str_equal (g_ptr_array_index (priv->selection, i), uuid))
            {
                g_ptr_array_remove_index (priv->selection, i);
                break;
            }
        }
        gtk_list_box_unselect_row (priv->list_box, row);
    }
    else
    {
        if (uuid)
            g_ptr_array_add (priv->selection, g_strdup (uuid));
        gtk_list_box_select_row (priv->list_box, row);
    }
}

static void
g_paste_ui_history_dispose (GObject *object)
{
    GPasteUiHistory *self = G_PASTE_UI_HISTORY (object);
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    g_clear_slist (&priv->items, g_object_unref);
    g_clear_pointer (&priv->selection, g_ptr_array_unref);

    g_clear_pointer (&priv->search, g_free);
    g_clear_pointer (&priv->search_results, g_strfreev);
    g_clear_object (&priv->client);
    g_clear_object (&priv->settings);

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
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    priv->selection = g_ptr_array_new_with_free_func (g_free);
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
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (_G_PASTE_IS_UI_PANEL (panel), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = g_object_new (G_PASTE_TYPE_UI_HISTORY,
                                      "orientation", GTK_ORIENTATION_VERTICAL,
                                      NULL);
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (G_PASTE_UI_HISTORY (self));
    GtkBox *box = GTK_BOX (self);

    priv->client = g_object_ref (client);
    priv->settings = g_object_ref (settings);
    priv->panel = panel;
    priv->rootwin = rootwin;
    priv->limit = g_paste_settings_get_max_displayed_history_size (settings);

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

    g_signal_connect_object (settings,
                             "changed::" G_PASTE_MAX_DISPLAYED_HISTORY_SIZE_SETTING,
                             G_CALLBACK (g_paste_ui_history_update_height_request),
                             self, 0);
    g_signal_connect_object (client,
                             "update",
                             G_CALLBACK (g_paste_ui_history_on_update),
                             self, 0);

    g_paste_ui_history_on_update (client, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0, self);

    return self;
}
