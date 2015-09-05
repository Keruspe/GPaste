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

#include <gpaste-ui-history-actions.h>
#include <gpaste-ui-panel.h>

struct _GPasteUiPanel
{
    GtkBox parent_instance;
};

typedef struct
{
    GPasteClient           *client;
    GPasteSettings         *settings;
    GPasteUiHistoryActions *actions;

    GtkListBox             *list_box;
    GSList                 *histories;

    gulong                  activated_id;
    gulong                  button_pressed_id;
    gulong                  delete_history_id;
    gulong                  switch_history_id;
} GPasteUiPanelPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiPanel, g_paste_ui_panel, GTK_TYPE_BOX)

static int
history_equals (gconstpointer a,
                gconstpointer b)
{
    return g_strcmp0 (b, g_paste_ui_panel_history_get_history (a));
}

static GSList *
history_find (GSList      *histories,
              const gchar *history)
{
    return g_slist_find_custom (histories, history, history_equals);
}

static void
on_history_deleted (GPasteClient *client G_GNUC_UNUSED,
                    const gchar  *history,
                    gpointer      user_data)
{
    GPasteUiPanelPrivate *priv = user_data;

    if (!g_strcmp0 (history, DEFAULT_HISTORY))
        return;

    GSList *h = history_find (priv->histories, history);

    if (!h)
        return;

    priv->histories = g_slist_remove_link (priv->histories, h);
    gtk_container_remove (GTK_CONTAINER (priv->list_box), h->data);
}

static void
g_paste_ui_panel_add_history (GPasteUiPanelPrivate *priv,
                              const gchar          *history,
                              gboolean              select);

static void
on_history_switched (GPasteClient *client G_GNUC_UNUSED,
                     const gchar  *history,
                     gpointer      user_data)
{
    GPasteUiPanelPrivate *priv = user_data;

    g_paste_ui_panel_add_history (priv, history, TRUE);
}

static void
on_row_activated (GtkListBox    *panel     G_GNUC_UNUSED,
                  GtkListBoxRow *row,
                  gpointer       user_data G_GNUC_UNUSED)
{
    g_paste_ui_panel_history_activate (G_PASTE_UI_PANEL_HISTORY (row));
}

static void
g_paste_ui_panel_add_history (GPasteUiPanelPrivate *priv,
                              const gchar          *history,
                              gboolean              select)
{
    GtkContainer *c = GTK_CONTAINER (priv->list_box);

    GSList *concurrent = history_find (priv->histories, history);
    GtkListBoxRow *row;

    if (concurrent)
    {
        row = concurrent->data;
    }
    else
    {
        GtkWidget *h = g_paste_ui_panel_history_new (priv->client, history);

        g_object_ref (h);
        gtk_container_add (c, h);
        gtk_widget_show_all (h);

        priv->histories = g_slist_prepend (priv->histories, h);

        row = GTK_LIST_BOX_ROW (h);
    }

    if (select)
        gtk_list_box_select_row (priv->list_box, row);
}

static void
on_histories_ready (GObject      *source_object,
                    GAsyncResult *res,
                    gpointer      user_data)
{
    GPasteUiPanelPrivate *priv = user_data;
    g_auto(GStrv) histories = g_paste_client_list_histories_finish (G_PASTE_CLIENT (source_object), res, NULL);
    g_autofree gchar *current = g_strdup (g_paste_settings_get_history_name (priv->settings));

    g_paste_ui_panel_add_history (priv, DEFAULT_HISTORY, !g_strcmp0 (DEFAULT_HISTORY, current));
    for (GStrv h = histories; *h; ++h)
        g_paste_ui_panel_add_history (priv, *h, !g_strcmp0 (*h, current));
}

static gboolean
g_paste_ui_panel_button_press_event (GtkWidget      *widget G_GNUC_UNUSED,
                                     GdkEventButton *event,
                                     gpointer        user_data)
{
    GPasteUiPanelPrivate *priv = user_data;

    if (gdk_event_triggers_context_menu ((GdkEvent *) event))
    {
        g_paste_ui_history_actions_set_relative_to (priv->actions,
                                                    G_PASTE_UI_PANEL_HISTORY (gtk_list_box_get_row_at_y (priv->list_box, event->y)));
        gtk_widget_show_all (GTK_WIDGET (priv->actions));
    }

    return FALSE;
}

static void
g_paste_ui_panel_dispose (GObject *object)
{
    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (G_PASTE_UI_PANEL (object));

    if (priv->activated_id)
    {
        g_signal_handler_disconnect (priv->list_box, priv->activated_id);
        g_signal_handler_disconnect (priv->list_box, priv->button_pressed_id);
        priv->activated_id = 0;
    }

    if (priv->client)
    {
        g_signal_handler_disconnect (priv->client, priv->delete_history_id);
        g_signal_handler_disconnect (priv->client, priv->switch_history_id);
        g_clear_object (&priv->client);
    }

    g_clear_object (&priv->settings);

    G_OBJECT_CLASS (g_paste_ui_panel_parent_class)->dispose (object);
}

static void
g_paste_ui_panel_class_init (GPasteUiPanelClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_panel_dispose;
}

static void
g_paste_ui_panel_init (GPasteUiPanel *self)
{
    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (self);

    gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)), GTK_STYLE_CLASS_SIDEBAR);

    priv->list_box = GTK_LIST_BOX (gtk_list_box_new ());
    priv->activated_id = g_signal_connect (G_OBJECT (priv->list_box),
                                           "row-activated",
                                           G_CALLBACK (on_row_activated),
                                           NULL);
    priv->button_pressed_id = g_signal_connect (G_OBJECT (priv->list_box),
                                                "button-press-event",
                                                G_CALLBACK (g_paste_ui_panel_button_press_event),
                                                priv);

    gtk_box_pack_start (GTK_BOX (self), GTK_WIDGET (priv->list_box), TRUE, TRUE, 0);
}

/**
 * g_paste_ui_panel_new:
 * @client: a #GPasteClient instance
 * @settings: a #GPasteSettings instance
 * @rootwin: the root #GtkWindow
 *
 * Create a new instance of #GPasteUiPanel
 *
 * Returns: a newly allocated #GPasteUiPanel
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_panel_new (GPasteClient   *client,
                      GPasteSettings *settings,
                      GtkWindow      *rootwin)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_PANEL,
                                      "orientation", GTK_ORIENTATION_VERTICAL,
                                      NULL);
    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (G_PASTE_UI_PANEL (self));

    priv->client = g_object_ref (client);
    priv->settings = g_object_ref (settings);
    priv->actions = G_PASTE_UI_HISTORY_ACTIONS (g_paste_ui_history_actions_new (client, rootwin));

    priv->delete_history_id = g_signal_connect (priv->client,
                                                "delete-history",
                                                G_CALLBACK (on_history_deleted),
                                                priv);
    priv->switch_history_id = g_signal_connect (priv->client,
                                                "switch-history",
                                                G_CALLBACK (on_history_switched),
                                                priv);

    g_paste_client_list_histories (client, on_histories_ready, priv);

    return self;
}
