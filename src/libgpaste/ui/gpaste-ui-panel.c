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
    GtkListBox parent_instance;
};

typedef struct
{
    GPasteClient           *client;
    GPasteSettings         *settings;
    GPasteUiHistoryActions *actions;

    GSList                 *histories;

    gulong                  activated_id;
} GPasteUiPanelPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiPanel, g_paste_ui_panel, GTK_TYPE_LIST_BOX)

static void
on_row_activated (GtkListBox    *panel     G_GNUC_UNUSED,
                  GtkListBoxRow *row,
                  gpointer       user_data G_GNUC_UNUSED)
{
    g_paste_ui_panel_history_activate (G_PASTE_UI_PANEL_HISTORY (row));
}

static int
history_equals (gconstpointer a,
                gconstpointer b)
{
    return g_strcmp0 (b, g_paste_ui_panel_history_get_history (a));
}

static void
g_paste_ui_panel_add_history (GPasteUiPanel *self,
                              const gchar   *history,
                              const gchar   *current)
{
    GtkContainer *c = GTK_CONTAINER (self);
    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (self);

    if (g_slist_find_custom (priv->histories, history, history_equals))
        return;

    GtkWidget *h = g_paste_ui_panel_history_new (priv->client, history);

    g_object_ref (h);
    gtk_container_add (c, h);
    gtk_widget_show_all (h);

    priv->histories = g_slist_prepend (priv->histories, h);

    if (!g_strcmp0 (history, current))
        gtk_list_box_select_row (GTK_LIST_BOX (self), GTK_LIST_BOX_ROW (h));
}

static void
on_histories_ready (GObject      *source_object,
                    GAsyncResult *res,
                    gpointer      user_data)
{
    GPasteUiPanel *self = user_data;
    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (self);
    g_auto(GStrv) histories = g_paste_client_list_histories_finish (G_PASTE_CLIENT (source_object), res, NULL);
    g_autofree gchar *current = g_strdup (g_paste_settings_get_history_name (priv->settings));

    /* FIXME: un hardcode */
    g_paste_ui_panel_add_history (self, "history", current);
    for (GStrv h = histories; *h; ++h)
        g_paste_ui_panel_add_history (self, *h, current);
}

static gboolean
g_paste_ui_panel_button_press_event (GtkWidget      *widget,
                                     GdkEventButton *event)
{
    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (G_PASTE_UI_PANEL (widget));

    if (gdk_event_triggers_context_menu ((GdkEvent *) event))
    {
        g_paste_ui_history_actions_set_relative_to (priv->actions,
                                                    G_PASTE_UI_PANEL_HISTORY (gtk_list_box_get_row_at_y (GTK_LIST_BOX (widget),
                                                                                                         event->y)));
        gtk_widget_show_all (GTK_WIDGET (priv->actions));
    }

    return GTK_WIDGET_CLASS (g_paste_ui_panel_parent_class)->button_press_event (widget, event);
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
    g_clear_object (&priv->settings);

    G_OBJECT_CLASS (g_paste_ui_panel_parent_class)->dispose (object);
}

static void
g_paste_ui_panel_class_init (GPasteUiPanelClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_panel_dispose;
    GTK_WIDGET_CLASS (klass)->button_press_event = g_paste_ui_panel_button_press_event;
}

static void
g_paste_ui_panel_init (GPasteUiPanel *self)
{
    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (self);

    gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)), GTK_STYLE_CLASS_SIDEBAR);

    priv->activated_id = g_signal_connect (G_OBJECT (self),
                                           "row-activated",
                                           G_CALLBACK (on_row_activated),
                                           NULL);
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

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_PANEL, NULL);
    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (G_PASTE_UI_PANEL (self));

    priv->client = g_object_ref (client);
    priv->settings = g_object_ref (settings);
    priv->actions = G_PASTE_UI_HISTORY_ACTIONS (g_paste_ui_history_actions_new (client, rootwin));

    g_paste_client_list_histories (client, on_histories_ready, self);

    return self;
}
