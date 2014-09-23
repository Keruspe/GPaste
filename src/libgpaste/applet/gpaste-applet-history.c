/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-applet-history-private.h"

#include <glib/gi18n-lib.h>

struct _GPasteAppletHistoryPrivate
{
    GPasteClient     *client;
    GPasteSettings   *settings;

    GPasteAppletMenu *menu;

    GSList           *items;
    gsize             size;

    gboolean          text_mode;

    gulong            update_id;
    gulong            settings_changed_id;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteAppletHistory, g_paste_applet_history, G_TYPE_OBJECT)

/**
 * g_paste_applet_history_set_text_mode:
 * @self: a #GPasteAppletHistory instance
 * @value: Whether to enable text mode or not
 *
 * Enable extra codepaths for when the text will
 * be handled raw without trimming and such.
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_applet_history_set_text_mode (GPasteAppletHistory *self,
                                      gboolean             value)
{
    g_return_if_fail (G_PASTE_IS_APPLET_HISTORY (self));

    GPasteAppletHistoryPrivate *priv = g_paste_applet_history_get_instance_private (self);
    priv->text_mode = value;

    for (GSList *i = priv->items; i; i = g_slist_next (i))
        g_paste_applet_item_set_text_mode (i->data, value);
}

static void
g_paste_applet_history_ref_item (gpointer data,
                                 gpointer user_data G_GNUC_UNUSED)
{
    GPasteAppletItem *item = data;
    g_object_ref (item);
}

static void
g_paste_applet_history_add_list_to_menu (GSList           *list,
                                         GPasteAppletMenu *menu)
{
    g_slist_foreach (list, g_paste_applet_history_ref_item, NULL);
    g_paste_applet_menu_append (menu, list);
}

static void
g_paste_applet_history_remove_from_menu (gpointer data,
                                         gpointer user_data)
{
    GtkWidget *item = data;
    GtkContainer *menu = user_data;

    g_object_unref (item);
    gtk_container_remove (menu, item);
}

static void
g_paste_applet_history_drop_list (GSList           *list,
                                  GPasteAppletMenu *menu)
{
    g_slist_foreach (list, g_paste_applet_history_remove_from_menu, menu);
    g_slist_free (list);
}

typedef struct {
    GPasteAppletHistory *self;
    guint                refreshFrom;
} OnUpdateCallbackData;

static void
g_paste_applet_history_refresh_history (GObject      *source_object G_GNUC_UNUSED,
                                        GAsyncResult *res,
                                        gpointer      user_data)
{
    G_PASTE_CLEANUP_FREE OnUpdateCallbackData *data = user_data;
    GPasteAppletHistory *self = data->self;
    GPasteAppletHistoryPrivate *priv = g_paste_applet_history_get_instance_private (self);

    gsize old_size = priv->size;
    guint refreshTextBound = old_size;
    priv->size = MIN (g_paste_client_get_history_size_finish (priv->client, res, NULL),
                      g_paste_settings_get_max_displayed_history_size (priv->settings));
    if (old_size < priv->size)
    {
        for (gsize i = old_size; i < priv->size; ++i)
        {
            GPasteAppletItem *item = G_PASTE_APPLET_ITEM (g_paste_applet_item_new (priv->client, priv->settings, i));
            g_paste_applet_item_set_text_mode (item, priv->text_mode);
            priv->items = g_slist_append (priv->items, item);
        }
        g_paste_applet_history_add_list_to_menu (g_slist_nth (priv->items, old_size), priv->menu);
        refreshTextBound = old_size;
    }
    else if (old_size > priv->size)
    {
        if (priv->size)
        {
            GSList *last = g_slist_nth (priv->items, priv->size - 1);
            g_return_if_fail (last);
            g_paste_applet_history_drop_list (g_slist_next (last), priv->menu);
            last->next = NULL;
        }
        else
        {
            g_paste_applet_history_drop_list (priv->items, priv->menu);
            priv->items = NULL;
        }
        refreshTextBound = priv->size;
    }

    GSList *item = priv->items;
    for (guint i = 0; i < data->refreshFrom; ++i)
        item = g_slist_next (item);
    for (guint i = data->refreshFrom; item && i < refreshTextBound; ++i, item = g_slist_next (item))
        g_paste_applet_item_reset_text (item->data);
}

static void
g_paste_applet_history_on_update (GPasteClient      *client,
                                  GPasteUpdateAction action,
                                  GPasteUpdateTarget target,
                                  guint              position,
                                  gpointer           user_data)
{
    GPasteAppletHistory *self = user_data;
    GPasteAppletHistoryPrivate *priv = g_paste_applet_history_get_instance_private (self);
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
            g_paste_applet_item_reset_text (g_slist_nth_data (priv->items, position));
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
    {
        OnUpdateCallbackData *data = g_new (OnUpdateCallbackData, 1);
        data->self = self;
        data->refreshFrom = position;
        g_paste_client_get_history_size (client, g_paste_applet_history_refresh_history, data);
    }
}

static void
g_paste_applet_history_on_settings_changed (GPasteSettings *settings G_GNUC_UNUSED,
                                            const gchar    *key      G_GNUC_UNUSED,
                                            gpointer        user_data)
{
    GPasteAppletHistory *self = user_data;
    GPasteAppletHistoryPrivate *priv = g_paste_applet_history_get_instance_private (self);
    g_paste_client_get_history_size (priv->client, g_paste_applet_history_refresh_history, self);
}

static void
g_paste_applet_history_dispose (GObject *object)
{
    GPasteAppletHistoryPrivate *priv = g_paste_applet_history_get_instance_private ((GPasteAppletHistory *) object);

    if (priv->client)
    {
        g_signal_handler_disconnect (priv->client, priv->update_id);
        g_clear_object (&priv->client);
    }

    if (priv->settings)
    {
        g_signal_handler_disconnect (priv->settings, priv->settings_changed_id);
        g_clear_object (&priv->settings);
    }

    if (priv->items) {
        g_paste_applet_history_drop_list (priv->items, priv->menu);
        priv->items = NULL;
    }

    G_OBJECT_CLASS (g_paste_applet_history_parent_class)->dispose (object);
}

static void
g_paste_applet_history_class_init (GPasteAppletHistoryClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_applet_history_dispose;
}

static void
g_paste_applet_history_init (GPasteAppletHistory *self)
{
    GPasteAppletHistoryPrivate *priv = g_paste_applet_history_get_instance_private (self);

    priv->text_mode = FALSE;

    priv->items = NULL;
    priv->size = 0;
}

/**
 * g_paste_applet_history_new:
 * @client: a #GPasteClient instance
 * @settings: a #GPasteSettings instance
 * @menu: the #GPasteAppletMenu we'll be attached to
 *
 * Create a new instance of #GPasteAppletHistory
 *
 * Returns: a newly allocated #GPasteAppletHistory
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteAppletHistory *
g_paste_applet_history_new (GPasteClient       *client,
                            GPasteSettings     *settings,
                            GPasteAppletMenu   *menu)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (G_PASTE_IS_APPLET_MENU (menu), NULL);

    GPasteAppletHistory *self = G_PASTE_APPLET_HISTORY (g_object_new (G_PASTE_TYPE_APPLET_HISTORY, NULL));
    GPasteAppletHistoryPrivate *priv = g_paste_applet_history_get_instance_private (self);

    priv->client = g_object_ref (client);
    priv->settings = g_object_ref (settings);
    priv->menu = menu;

    priv->update_id = g_signal_connect (client,
                                        "update",
                                        G_CALLBACK (g_paste_applet_history_on_update),
                                        self);
    priv->settings_changed_id = g_signal_connect (settings,
                                                  "changed::" G_PASTE_MAX_DISPLAYED_HISTORY_SIZE_SETTING,
                                                  G_CALLBACK (g_paste_applet_history_on_settings_changed),
                                                  self);
    g_paste_applet_history_on_update (client, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0, self);

    return self;
}
