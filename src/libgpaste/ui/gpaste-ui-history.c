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

#include "gpaste-ui-history-private.h"

struct _GPasteUiHistoryPrivate
{
    GPasteSettings *settings;

    gulong          activated_id;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiHistory, g_paste_ui_history, GTK_TYPE_LIST_BOX)

static void
on_row_activated (GtkListBox    *history G_GNUC_UNUSED,
                  GtkListBoxRow *row G_GNUC_UNUSED,
                  gpointer       user_data G_GNUC_UNUSED)
{
}

static void
g_paste_ui_history_dispose (GObject *object)
{
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (G_PASTE_UI_HISTORY (object));

    g_clear_object (&priv->settings);

    if (priv->activated_id)
    {
        g_signal_handler_disconnect (object, priv->activated_id);
        priv->activated_id = 0;
    }

    G_OBJECT_CLASS (g_paste_ui_history_parent_class)->dispose (object);
}

static void
g_paste_ui_history_class_init (GPasteUiHistoryClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_history_dispose;
}

static void
g_paste_ui_history_init (GPasteUiHistory *self)
{
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (self);

    priv->settings = g_paste_settings_new ();

    priv->activated_id = g_signal_connect (G_OBJECT (self),
                                           "row-activated",
                                           G_CALLBACK (on_row_activated),
                                           self);
}

/**
 * g_paste_ui_history_new:
 * @client: a #GPasteClient instance
 *
 * Create a new instance of #GPasteUiHistory
 *
 * Returns: a newly allocated #GPasteUiHistory
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_history_new (GPasteClient *client)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_HISTORY, NULL);
    GPasteUiHistoryPrivate *priv = g_paste_ui_history_get_instance_private (G_PASTE_UI_HISTORY (self));
    GtkContainer *lb = GTK_CONTAINER (self);

    for (guint32 i = 0; i < 20 /* FIXME */; ++i)
        gtk_container_add (lb, g_paste_ui_item_new (client, priv->settings, i));

    return self;
}
