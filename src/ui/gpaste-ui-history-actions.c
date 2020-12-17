/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-backup-history.h>
#include <gpaste-ui-delete-history.h>
#include <gpaste-ui-empty-history.h>
#include <gpaste-ui-history-actions.h>

#include "gpaste-gtk-compat.h"

struct _GPasteUiHistoryActions
{
    GtkPopover parent_instance;
};

typedef struct
{
    GPasteClient *client;

    GSList       *actions;
} GPasteUiHistoryActionsPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UiHistoryActions, ui_history_actions, GTK_TYPE_POPOVER)

static void
action_set_history (gpointer data,
                    gpointer user_data)
{
    GPasteUiHistoryAction *action = data;
    const gchar *history = user_data;

    g_paste_ui_history_action_set_history (action, history);
}

/**
 * g_paste_ui_history_actions_set_relative_to:
 * @self: the #GPasteUiHistoryActions
 * @history: (nullable): a #GPasteUiPanelHistory instance
 *
 * Set which history we'll deal with
 */
G_PASTE_VISIBLE void
g_paste_ui_history_actions_set_relative_to (GPasteUiHistoryActions *self,
                                            GPasteUiPanelHistory   *history)
{
    g_return_if_fail (_G_PASTE_IS_UI_HISTORY_ACTIONS (self));
    g_return_if_fail (!history || _G_PASTE_IS_UI_PANEL_HISTORY (history));

    const GPasteUiHistoryActionsPrivate *priv = _g_paste_ui_history_actions_get_instance_private (self);
    const gchar *h = (history) ? g_paste_ui_panel_history_get_history (history) : NULL;

    g_slist_foreach (priv->actions, action_set_history, (gpointer) h);

    if (history)
        gtk_popover_set_relative_to (GTK_POPOVER (self), GTK_WIDGET (history));
    else
        gtk_widget_hide (GTK_WIDGET (self));
}

static void
add_action_to_box (gpointer data,
                   gpointer user_data)
{
    GtkContainer *box = user_data;
    GtkWidget *action = data;

    gtk_container_add (box, action);
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
 * @settings: a #GPasteSettings instance
 * @rootwin: the root #GtkWindow
 *
 * Create a new instance of #GPasteUiHistoryActions
 *
 * Returns: a newly allocated #GPasteUiHistoryActions
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_history_actions_new (GPasteClient   *client,
                                GPasteSettings *settings,
                                GtkWindow      *rootwin)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_HISTORY_ACTIONS,
                                      "width-request",  200,
                                      "height-request", 40,
                                      NULL);
    GPasteUiHistoryActionsPrivate *priv = g_paste_ui_history_actions_get_instance_private (G_PASTE_UI_HISTORY_ACTIONS (self));
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *backup = g_paste_ui_backup_history_new (client, settings, self, rootwin);
    GtkWidget *delete = g_paste_ui_delete_history_new (client, settings, self, rootwin);
    GtkWidget *empty = g_paste_ui_empty_history_new (client, settings, self, rootwin);

    priv->client = g_object_ref (client);
    priv->actions = g_slist_append (priv->actions, backup);
    priv->actions = g_slist_append (priv->actions, empty);
    priv->actions = g_slist_append (priv->actions, delete);

    gtk_popover_set_position (GTK_POPOVER (self), GTK_POS_RIGHT);

    g_slist_foreach (priv->actions, add_action_to_box, box);
    gtk_widget_set_margin_top (box, 5);
    gtk_widget_set_margin_bottom (box, 5);
    gtk_widget_show_all (box);

    gtk_container_add (GTK_CONTAINER (self), box);

    return self;
}
