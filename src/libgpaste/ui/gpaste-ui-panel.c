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

#include <gpaste-ui-panel.h>
#include <gpaste-ui-panel-history.h>

struct _GPasteUiPanel
{
    GtkListBox parent_instance;
};

typedef struct
{
    GPasteClient   *client;

    gulong          activated_id;
} GPasteUiPanelPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiPanel, g_paste_ui_panel, GTK_TYPE_LIST_BOX)

static void
on_row_activated (GtkListBox    *panel     G_GNUC_UNUSED,
                  GtkListBoxRow *row,
                  gpointer       user_data G_GNUC_UNUSED)
{
    g_paste_ui_panel_history_activate (G_PASTE_UI_PANEL_HISTORY (row));
}

static void
g_paste_ui_panel_add_history (GPasteUiPanel *self,
                              const gchar   *history)
{
    GtkContainer *c = GTK_CONTAINER (self);
    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (self);
    GtkWidget *h = g_paste_ui_panel_history_new (priv->client, history);

    g_object_ref (h);
    gtk_container_add (c, h);
    gtk_widget_show_all (h);
}

static void
on_histories_ready (GObject      *source_object,
                    GAsyncResult *res,
                    gpointer      user_data)
{
    GPasteUiPanel *self = user_data;
    g_auto(GStrv) histories = g_paste_client_list_histories_finish (G_PASTE_CLIENT (source_object), res, NULL);

    for (GStrv h = histories; *h; ++h)
        g_paste_ui_panel_add_history (self, *h);
}

static void
g_paste_ui_panel_dispose (GObject *object)
{
    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (G_PASTE_UI_PANEL (object));

    if (priv->activated_id)
    {
        g_signal_handler_disconnect (object, priv->activated_id);
        priv->activated_id = 0;
    }

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_panel_parent_class)->dispose (object);
}

static void
g_paste_ui_panel_class_init (GPasteUiPanelClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_ui_panel_dispose;
}

static void
g_paste_ui_panel_init (GPasteUiPanel *self)
{
    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (self);
    GtkStyleContext *context = gtk_widget_get_style_context (GTK_WIDGET (self));

    gtk_style_context_add_class (context, "sidebar");

    priv->activated_id = g_signal_connect (G_OBJECT (self),
                                           "row-activated",
                                           G_CALLBACK (on_row_activated),
                                           NULL);
}

/**
 * g_paste_ui_panel_new:
 * @client: a #GPasteClient instance
 *
 * Create a new instance of #GPasteUiPanel
 *
 * Returns: a newly allocated #GPasteUiPanel
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_panel_new (GPasteClient *client)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_PANEL, NULL);
    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (G_PASTE_UI_PANEL (self));

    priv->client = g_object_ref (client);

    g_paste_client_list_histories (client, on_histories_ready, self);

    return self;
}
