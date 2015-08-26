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

#include <gpaste-ui-panel-history.h>

struct _GPasteUiPanelHistory
{
    GtkListBoxRow parent_instance;
};

typedef struct
{
    GPasteClient *client;

    GtkLabel     *label;

    gchar        *history;
} GPasteUiPanelHistoryPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiPanelHistory, g_paste_ui_panel_history, GTK_TYPE_LIST_BOX_ROW)

/**
 * g_paste_ui_panel_history_activate:
 * @self: a #GPasteUiPanelHistory instance
 *
 * Refresh the panel_history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_panel_history_activate (GPasteUiPanelHistory *self)
{
    g_return_if_fail (G_PASTE_IS_UI_PANEL_HISTORY (self));
    GPasteUiPanelHistoryPrivate *priv = g_paste_ui_panel_history_get_instance_private (self);

    g_paste_client_switch_history (priv->client, priv->history, NULL, NULL);
}

static void
g_paste_ui_panel_history_dispose (GObject *object)
{
    GPasteUiPanelHistoryPrivate *priv = g_paste_ui_panel_history_get_instance_private (G_PASTE_UI_PANEL_HISTORY (object));

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_panel_history_parent_class)->dispose (object);
}

static void
g_paste_ui_panel_history_class_init (GPasteUiPanelHistoryClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_panel_history_dispose;
}

static void
g_paste_ui_panel_history_init (GPasteUiPanelHistory *self)
{
    GPasteUiPanelHistoryPrivate *priv = g_paste_ui_panel_history_get_instance_private (self);
    GtkWidget *l = gtk_label_new ("");
    GtkLabel *label = priv->label = GTK_LABEL (l);

    gtk_label_set_ellipsize (label, PANGO_ELLIPSIZE_END);
    gtk_container_add (GTK_CONTAINER (self), l);
}

/**
 * g_paste_ui_panel_history_new:
 * @client: a #GPasteClient instance
 * @history: the history we represent
 *
 * Create a new instance of #GPasteUiPanelHistory
 *
 * Returns: a newly allocated #GPasteUiPanelHistory
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_panel_history_new (GPasteClient *client,
                              const gchar  *history)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_if_fail (g_utf8_validate (history, -1, NULL));

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_PANEL_HISTORY,
                                      "width-request",  100,
                                      "height-request", 50,
                                      NULL);
    GPasteUiPanelHistoryPrivate *priv = g_paste_ui_panel_history_get_instance_private (G_PASTE_UI_PANEL_HISTORY (self));

    priv->client = g_object_ref (client);
    priv->history = g_strdup (history);

    gtk_label_set_text (priv->label, history);

    return self;
}
