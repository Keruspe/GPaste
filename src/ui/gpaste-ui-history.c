/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gsettings-keys.h>
#include <gpaste-ui-empty-item.h>
#include <gpaste-ui-history.h>
#include <gpaste-ui-item.h>
#include <gpaste-update-enums.h>

#include "gpaste-gtk-compat.h"

struct _GPasteUiHistory
{
    GtkListBox parent_instance;
};

enum
{
    C_SIZE,
    C_UPDATE,

    C_LAST_SIGNAL
};

typedef struct
{
    GPasteClient      *client;
    GPasteSettings    *settings;
    GPasteUiPanel     *panel;
    GPasteUiEmptyItem *dummy_item;

    GtkWindow         *rootwin;

    GSList            *items;
    guint64            size;
    gint32             item_height;

    gchar             *search;
    GStrv              search_results;

    guint64            c_signals[C_LAST_SIGNAL];
} GPasteUiHistoryPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UiHistory, ui_history, GTK_TYPE_LIST_BOX)

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
    GtkContainer *self = user_data;
    GtkWidget *item = data;

    g_object_ref (item);
    gtk_container_add (self, item);
    gtk_widget_show_all (item);
}

static void
g_paste_ui_history_add_list (GtkContainer *self,
                             GSList       *list)
{
    g_slist_foreach (list, g_paste_ui_history_add_item, self);
}

static void
g_paste_ui_history_remove (gpointer data,
                           gpointer user_data)
{
    GtkWidget *item = data;
    GtkContainer *self = user_data;

    gtk_container_remove (self, item);
    g_object_unref (item);
}

static void
g_paste_ui_history_drop_list (GtkContainer *self,
                              GSList       *list)
{
    g_slist_foreach (list, g_paste_ui_history_remove, self);
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
    const GPasteUiHistoryPrivate *priv = _g_paste_ui_history_get_instance_private (self);
    guint64 new_size = g_paste_settings_get_max_displayed_history_size (settings);

    if (priv->item_height)
        g_object_set (G_OBJECT (self), "height-request", new_size * priv->item_height, NULL);

    if (new_size != priv->size)
        g_paste_ui_history_refresh (self, 0);
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
    g_autofree OnUpdateCallbackData *data = user_data;
    g_autofree gchar *name = data->name;
    GPasteUiHistory *self = data->self;
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    guint64 old_size = priv->size;
    guint64 refreshTextBound = old_size;
    guint64 new_size = g_paste_client_get_history_size_finish (priv->client, res, NULL);
    guint64 max_size = g_paste_settings_get_max_displayed_history_size (priv->settings);

    priv->size = MIN (new_size, max_size);

    if (priv->size)
        gtk_widget_hide (GTK_WIDGET (priv->dummy_item));
    else
        g_paste_ui_empty_item_show_empty (priv->dummy_item);

    g_paste_ui_panel_update_history_length (priv->panel, name, new_size);

    if (old_size < priv->size)
    {
        for (guint64 i = old_size; i < priv->size; ++i)
        {
            GtkWidget *item = g_paste_ui_item_new (priv->client, priv->settings, priv->rootwin, i);
            priv->items = g_slist_append (priv->items, item);
        }
        g_paste_ui_history_add_list (GTK_CONTAINER (self), g_slist_nth (priv->items, old_size));
        refreshTextBound = old_size;
    }
    else if (old_size > priv->size)
    {
        if (priv->size)
        {
            GSList *last = g_slist_nth (priv->items, priv->size - 1);
            g_return_if_fail (last);
            g_paste_ui_history_drop_list (GTK_CONTAINER (self), g_slist_next (last));
            last->next = NULL;
        }
        else
        {
            g_paste_ui_history_drop_list (GTK_CONTAINER (self), priv->items);
            priv->items = NULL;
        }
        refreshTextBound = priv->size;
    }

    GSList *item = priv->items;

    for (guint64 i = 0; i < data->from_index; ++i)
        item = g_slist_next (item);
    for (guint64 i = data->from_index; i < refreshTextBound && item; ++i, item = g_slist_next (item))
        g_paste_ui_item_set_index (item->data, i);

    if (!priv->item_height)
    {
        gtk_widget_measure (GTK_WIDGET ((priv->items) ? priv->items->data : priv->dummy_item), GTK_ORIENTATION_VERTICAL, -1, NULL, &priv->item_height, NULL, NULL);
        g_paste_ui_history_update_height_request (priv->settings, NULL, self);
    }
}

static void
on_name_ready (GObject      *source_object G_GNUC_UNUSED,
               GAsyncResult *res,
               gpointer      user_data)
{
    OnUpdateCallbackData *data = user_data;
    const GPasteUiHistoryPrivate *priv = _g_paste_ui_history_get_instance_private (data->self);

    data->name = g_paste_client_get_history_name_finish (priv->client, res, NULL);

    g_paste_client_get_history_size (priv->client, data->name, g_paste_ui_history_refresh_history, data);
}

static void
g_paste_ui_history_refresh (GPasteUiHistory *self,
                            guint64          from_index)
{
    const GPasteUiHistoryPrivate *priv = _g_paste_ui_history_get_instance_private (self);

    if (priv->search)
    {
        g_paste_ui_history_search (self, priv->search);
    }
    else
    {
        OnUpdateCallbackData *data = g_new (OnUpdateCallbackData, 1);
        data->self = self;
        data->from_index = from_index;

        g_paste_client_get_history_name (priv->client, on_name_ready, data);
    }
}

static void
on_search_ready (GObject      *source_object G_GNUC_UNUSED,
                 GAsyncResult *res,
                 gpointer      user_data)
{
    GPasteUiHistory *self = user_data;
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);
    GSList *item = priv->items;

    g_clear_pointer (&priv->search_results, g_strfreev);
    priv->search_results = g_paste_client_search_finish (priv->client, res, NULL /* error */);
    guint64 search_results_size = g_strv_length (priv->search_results);

    if (priv->search_results)
    {
        if (search_results_size > priv->size)
            search_results_size = priv->size;

        for (guint64 i = 0; i < search_results_size; ++i, item = g_slist_next (item))
            g_paste_ui_item_set_uuid (item->data, priv->search_results[i]);
    }
    else
    {
        g_paste_ui_empty_item_show_no_result (priv->dummy_item);
    }

    for (guint64 i = search_results_size; i < priv->size; ++i, item = g_slist_next (item))
        g_paste_ui_item_set_index (item->data, (guint64) -1);
}

/**
 * g_paste_ui_history_search:
 * @self: a #GPasteUiHistory instance
 * @search: the search
 *
 * Apply a search to a #GPasteUiHistory instance
 */
G_PASTE_VISIBLE void
g_paste_ui_history_search (GPasteUiHistory *self,
                           const gchar     *search)
{
    g_return_if_fail (_G_PASTE_IS_UI_HISTORY (self));

    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    if (g_paste_str_equal (search, ""))
    {
        g_clear_pointer (&priv->search, g_free);
        g_clear_pointer (&priv->search_results, g_strfreev);
        g_paste_ui_history_refresh (self, 0);
    }
    else
    {
        if (search != priv->search)
        {
            g_free (priv->search);
            priv->search = g_strdup (search);
        }
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
    g_return_val_if_fail (_G_PASTE_IS_UI_HISTORY (self), FALSE);

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
    const GPasteUiHistoryPrivate *priv = _g_paste_ui_history_get_instance_private (self);
    gboolean refresh = FALSE;

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
g_paste_ui_history_dispose (GObject *object)
{
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (G_PASTE_UI_HISTORY (object));

    if (priv->settings)
    {
        g_signal_handler_disconnect (priv->settings, priv->c_signals[C_SIZE]);
        g_clear_object (&priv->settings);
    }

    if (priv->client)
    {
        g_signal_handler_disconnect (priv->client, priv->c_signals[C_UPDATE]);
        g_clear_object (&priv->client);
    }

    G_OBJECT_CLASS (g_paste_ui_history_parent_class)->dispose (object);
}

static void
g_paste_ui_history_finalize (GObject *object)
{
    const GPasteUiHistoryPrivate *priv = _g_paste_ui_history_get_instance_private (G_PASTE_UI_HISTORY (object));

    g_free (priv->search);
    g_strfreev (priv->search_results);

    G_OBJECT_CLASS (g_paste_ui_history_parent_class)->finalize (object);
}

static void
g_paste_ui_history_class_init (GPasteUiHistoryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_ui_history_dispose;
    object_class->finalize = g_paste_ui_history_finalize;

    GTK_LIST_BOX_CLASS (klass)->row_activated = on_row_activated;
}

static void
g_paste_ui_history_init (GPasteUiHistory *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_ui_history_new:
 * @client: a #GPasteClient instance
 * @settings: a #GPasteSettings instance
 * @panel: the #GPasteSettingsUiPanel
 * @rootwin: the root #GtkWindow
 *
 * Create a new instance of #GPasteUiHistory
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

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_HISTORY, NULL);
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (G_PASTE_UI_HISTORY (self));
    GtkWidget *dummy_item = g_paste_ui_empty_item_new (client, settings, rootwin);

    priv->client = g_object_ref (client);
    priv->settings = g_object_ref (settings);
    priv->dummy_item = G_PASTE_UI_EMPTY_ITEM (dummy_item);
    priv->panel = panel;
    priv->rootwin = rootwin;

    gtk_container_add (GTK_CONTAINER (self), dummy_item);

    priv->c_signals[C_SIZE] = g_signal_connect (settings,
                                                "changed::" G_PASTE_MAX_DISPLAYED_HISTORY_SIZE_SETTING,
                                                G_CALLBACK (g_paste_ui_history_update_height_request),
                                                self);
    priv->c_signals[C_UPDATE] = g_signal_connect (client,
                                                  "update",
                                                  G_CALLBACK (g_paste_ui_history_on_update),
                                                  self);

    g_paste_ui_history_on_update (client, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0, self);

    return self;
}
