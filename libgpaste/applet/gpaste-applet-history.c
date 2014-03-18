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

static void
g_paste_applet_history_refresh_history (GObject      *source_object G_GNUC_UNUSED,
                                        GAsyncResult *res,
                                        gpointer      user_data)
{
    GPasteAppletHistory *self = user_data;
    GPasteAppletHistoryPrivate *priv = g_paste_applet_history_get_instance_private (self);

    gsize old_size = priv->size;
    priv->size = MIN (g_paste_client_get_history_size_finish (priv->client, res, NULL),
                      g_paste_settings_get_max_displayed_history_size (priv->settings));
    if (old_size < priv->size)
    {
        for (gsize i = old_size; i < priv->size; ++i)
            priv->items = g_slist_append (priv->items, g_paste_applet_item_new (priv->client, priv->settings, i));
        g_paste_applet_history_add_list_to_menu (g_slist_nth (priv->items, old_size), priv->menu);
    }
    else if (old_size > priv->size)
    {
        if (priv->size)
        {
            GSList *last = g_slist_nth (priv->items, priv->size - 1);
            g_paste_applet_history_drop_list (g_slist_next (last), priv->menu);
            last->next = NULL;
        }
        else
        {
            g_paste_applet_history_drop_list (priv->items, priv->menu);
            priv->items = NULL;
        }
    }
}

static void
g_paste_applet_history_on_changed (GPasteClient *client,
                                   gpointer      user_data)
{
    GPasteAppletHistory *self = user_data;
    g_paste_client_get_history_size (client, g_paste_applet_history_refresh_history, self);
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

    g_clear_object (&priv->settings);

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

    priv->items = NULL;
    priv->size = 0;
    priv->changed_id = 0;
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

    priv->changed_id = g_signal_connect (G_OBJECT (client),
                                         "changed",
                                         G_CALLBACK (g_paste_applet_history_on_changed),
                                         self);
    g_paste_applet_history_on_changed (client, self);

    return self;
}
