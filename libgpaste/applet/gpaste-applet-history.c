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
    GPasteAppletMenu *menu;

    GSList           *items;
    gsize             size;

    gulong            changed_id;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteAppletHistory, g_paste_applet_history, G_TYPE_OBJECT)

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
    g_paste_applet_menu_append_list (menu, list);
}

static void
g_paste_applet_history_private_add_history (GPasteAppletHistoryPrivate *priv,
                                            gchar                     **history) /* FIXME: const */
{
    priv->size = g_strv_length (history);

    for (gsize i = 0; i < priv->size; ++i)
        priv->items = g_slist_append (priv->items, g_paste_applet_item_new (priv->client, i));

    g_paste_applet_history_add_list_to_menu (priv->items, priv->menu);
}

static void
g_paste_applet_history_on_history_ready (GObject      *source_object G_GNUC_UNUSED,
                                         GAsyncResult *res,
                                         gpointer      user_data)
{
    GPasteAppletHistoryPrivate *priv = user_data;
    g_paste_applet_history_private_add_history (priv, g_paste_client_get_history_finish (priv->client, res, NULL));
}

static void
g_paste_applet_history_remove_from_menu (gpointer data,
                                         gpointer user_data)
{
    GtkWidget *item = data;
    GtkContainer *menu = user_data;

    gtk_container_remove (menu, item);
}

static void
g_paste_applet_history_drop_list (GSList           *list,
                                  GPasteAppletMenu *menu)
{
    g_slist_foreach (list, g_paste_applet_history_remove_from_menu, menu);
    g_slist_free_full (list, g_object_unref);
}

static void
g_paste_applet_history_refresh_history (GObject      *source_object G_GNUC_UNUSED,
                                        GAsyncResult *res,
                                        gpointer      user_data)
{
    GPasteAppletHistory *self = user_data;
    GPasteAppletHistoryPrivate *priv = g_paste_applet_history_get_instance_private (self);
    gchar **history = g_paste_client_get_history_finish (priv->client, res, NULL);

    gsize old_size = priv->size;
    priv->size = g_strv_length (history);
    if (old_size < priv->size)
    {
        for (gsize i = old_size; i < priv->size; ++i)
            priv->items = g_slist_append (priv->items, g_paste_applet_item_new (priv->client, i));
        g_paste_applet_history_add_list_to_menu (g_slist_nth (priv->items, old_size), priv->menu);
    }
    else if (old_size > priv->size)
    {
        GSList *last = g_slist_nth (priv->items, priv->size - 1);
        g_paste_applet_history_drop_list (g_slist_next (last), priv->menu);
        last->next = NULL;
    }
}

static void
g_paste_applet_history_on_changed (GPasteClient *client,
                                   gpointer      user_data)
{
    GPasteAppletHistory *self = user_data;
    g_paste_client_get_history (client, g_paste_applet_history_refresh_history, self);
}

static void
g_paste_applet_history_dispose (GObject *object)
{
    GPasteAppletHistoryPrivate *priv = g_paste_applet_history_get_instance_private ((GPasteAppletHistory *) object);

    if (priv->client)
    {
        g_signal_handler_disconnect (priv->client, priv->changed_id);
        g_clear_object (&priv->client);
    }

    if (priv->items) {
        g_paste_applet_history_drop_list (priv->items, priv->menu);
        priv->items = NULL;
    }

    g_clear_object (&priv->menu);
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

    priv->items = NULL;
}

static GPasteAppletHistory *
_g_paste_applet_history_new (GPasteClient     *client,
                             GPasteAppletMenu *menu)
{
    GPasteAppletHistory *self = G_PASTE_APPLET_HISTORY (g_object_new (G_PASTE_TYPE_APPLET_HISTORY, NULL));
    GPasteAppletHistoryPrivate *priv = g_paste_applet_history_get_instance_private (self);
    priv->client = g_object_ref (client);
    priv->menu = g_object_ref (menu);
    priv->changed_id = g_signal_connect (G_OBJECT (client),
                                         "changed",
                                         G_CALLBACK (g_paste_applet_history_on_changed),
                                         self);
    return self;
}

/**
 * g_paste_applet_history_new:
 * @client: a #GPasteClient
 * @menu: the #GPasteAppletMenu we'll be attached to
 *
 * Create a new instance of #GPasteAppletHistory
 *
 * Returns: a newly allocated #GPasteAppletHistory
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteAppletHistory *
g_paste_applet_history_new (GPasteClient     *client, /* FIXME: asynccallback */
                            GPasteAppletMenu *menu)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (G_PASTE_IS_APPLET_MENU (menu), NULL);

    GPasteAppletHistory *self = _g_paste_applet_history_new (client, menu);
    GPasteAppletHistoryPrivate *priv = g_paste_applet_history_get_instance_private (self);

    g_paste_client_get_history (priv->client, g_paste_applet_history_on_history_ready, priv);

    return self;
}

/**
 * g_paste_applet_history_new_sync:
 * @client: a #GPasteClient
 * @menu: the #GPasteAppletMenu we'll be attached to
 *
 * Create a new instance of #GPasteAppletHistory
 *
 * Returns: a newly allocated #GPasteAppletHistory
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteAppletHistory *
g_paste_applet_history_new_sync (GPasteClient     *client,
                                 GPasteAppletMenu *menu)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (G_PASTE_IS_APPLET_MENU (menu), NULL);

    GPasteAppletHistory *self = _g_paste_applet_history_new (client, menu);
    GPasteAppletHistoryPrivate *priv = g_paste_applet_history_get_instance_private (self);

    g_paste_applet_history_private_add_history (priv, g_paste_client_get_history_sync (client, NULL));

    return self;
}
