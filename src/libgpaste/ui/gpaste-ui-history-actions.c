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
#include <gpaste-ui-delete-history.h>

struct _GPasteUiHistoryActions
{
    GtkPopover parent_instance;
};

typedef struct
{
    GPasteClient          *client;

    /* TODO: make a GPasteUiHistoryAction */
    GPasteUiDeleteHistory *delete;
} GPasteUiHistoryActionsPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiHistoryActions, g_paste_ui_history_actions, GTK_TYPE_POPOVER)

/**
 * g_paste_ui_history_actions_set_relative_to:
 * @self: the #GPasteUiHistoryActions
 * @history: (nullable): a #GPasteUiPanelHistory instance
 *
 * Set which history we'll deal with
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_history_actions_set_relative_to (GPasteUiHistoryActions *self,
                                            GPasteUiPanelHistory   *history)
{
    g_return_if_fail (G_PASTE_IS_UI_HISTORY_ACTIONS (self));
    g_return_if_fail (!history || G_PASTE_IS_UI_PANEL_HISTORY (history));

    GPasteUiHistoryActionsPrivate *priv = g_paste_ui_history_actions_get_instance_private (self);
    const gchar *h = (history) ? g_paste_ui_panel_history_get_history (history) : NULL;

    gtk_popover_set_relative_to (GTK_POPOVER (self), GTK_WIDGET (history));
    g_paste_ui_delete_history_set_history (priv->delete, h);
}

static void
g_paste_ui_history_actions_dispose (GObject *object)
{
    GPasteUiHistoryActionsPrivate *priv = g_paste_ui_history_actions_get_instance_private (G_PASTE_UI_HISTORY_ACTIONS (object));

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_history_actions_parent_class)->dispose (object);
}

static void
g_paste_ui_history_actions_class_init (GPasteUiHistoryActionsClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_history_actions_dispose;
}

static void
g_paste_ui_history_actions_init (GPasteUiHistoryActions *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_ui_history_actions_new:
 * @client: a #GPasteClient instance
 * @rootwin: the root #GtkWindow
 *
 * Create a new instance of #GPasteUiHistoryActions
 *
 * Returns: a newly allocated #GPasteUiHistoryActions
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_history_actions_new (GPasteClient *client,
                                GtkWindow    *rootwin)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_HISTORY_ACTIONS,
                                      "width-request",  200,
                                      "height-request", 150,
                                      NULL);
    GPasteUiHistoryActionsPrivate *priv = g_paste_ui_history_actions_get_instance_private (G_PASTE_UI_HISTORY_ACTIONS (self));
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *delete = g_paste_ui_delete_history_new (client, rootwin);
    GtkContainer *b = GTK_CONTAINER (box);

    priv->client = g_object_ref (client);

    priv->delete = G_PASTE_UI_DELETE_HISTORY (delete);

    gtk_popover_set_position (GTK_POPOVER (self), GTK_POS_RIGHT);

    gtk_container_add (b, delete);
    gtk_widget_show_all (box);

    gtk_container_add (GTK_CONTAINER (self), box);

    return self;
}
