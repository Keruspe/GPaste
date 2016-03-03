/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gpaste-gsettings-keys.h>
#include <gpaste-ui-empty-item.h>
#include <gpaste-ui-history.h>
#include <gpaste-ui-item.h>
#include <gpaste-update-enums.h>

struct _GPasteUiHistory
{
    GtkListBox parent_instance;
};

typedef struct
{
    GPasteClient   *client;
    GPasteSettings *settings;
    GPasteUiPanel  *panel;
    GtkWidget      *dummy_item;

    GtkWindow      *rootwin;

    GSList         *items;
    guint64         size;
    gint32          item_height;

    gchar          *search;
    guint64        *search_results;
    guint64         search_results_size;

    guint64         size_id;
    guint64         update_id;
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
        gtk_widget_hide (priv->dummy_item);
    else
        gtk_widget_show (priv->dummy_item);

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
        gtk_widget_get_preferred_height ((priv->items) ? GTK_WIDGET (priv->items->data) : priv->dummy_item, NULL, &priv->item_height);
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

    priv->search_results = g_paste_client_search_finish (priv->client, res, &priv->search_results_size, NULL /* error */);

    if (!priv->search_results)
        priv->search_results_size = 0;
    else if (priv->search_results_size > priv->size)
        priv->search_results_size = priv->size;

    GSList *item = priv->items;

    for (guint64 i = 0; i < priv->search_results_size; ++i, item = g_slist_next (item))
        g_paste_ui_item_set_index (item->data, priv->search_results[i]);
    for (guint64 i = priv->search_results_size; i < priv->size; ++i, item = g_slist_next (item))
        g_paste_ui_item_set_index (item->data, -1);
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
        g_clear_pointer (&priv->search_results, g_free);
        priv->search_results_size = 0;
        g_paste_ui_history_refresh (self, 0);
    }
    else
    {
        g_free (priv->search);
        priv->search = g_strdup (search);
        g_paste_client_search (priv->client, search, on_search_ready, self);
    }
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
    const GPasteUiHistoryPrivate *priv = _g_paste_ui_history_get_instance_private (G_PASTE_UI_HISTORY (object));

    if (priv->settings)
    {
        g_signal_handler_disconnect (priv->settings, priv->size_id);
        g_clear_object (&priv->settings);
    }

    if (priv->client)
    {
        g_signal_handler_disconnect (priv->client, priv->update_id);
        g_clear_object (&priv->client);
    }

    G_OBJECT_CLASS (g_paste_ui_history_parent_class)->dispose (object);
}

static void
g_paste_ui_history_finalize (GObject *object)
{
    const GPasteUiHistoryPrivate *priv = _g_paste_ui_history_get_instance_private (G_PASTE_UI_HISTORY (object));

    g_free (priv->search);
    g_free (priv->search_results);

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

    priv->client = g_object_ref (client);
    priv->settings = g_object_ref (settings);
    priv->dummy_item = g_paste_ui_empty_item_new (client, settings, rootwin);
    priv->panel = panel;
    priv->rootwin = rootwin;

    gtk_container_add (GTK_CONTAINER (self), priv->dummy_item);

    priv->size_id = g_signal_connect (settings,
                                      "changed::" G_PASTE_MAX_DISPLAYED_HISTORY_SIZE_SETTING,
                                      G_CALLBACK (g_paste_ui_history_update_height_request),
                                      self);
    priv->update_id = g_signal_connect (client,
                                        "update",
                                        G_CALLBACK (g_paste_ui_history_on_update),
                                        self);

    g_paste_ui_history_on_update (client, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0, self);

    return self;
}
