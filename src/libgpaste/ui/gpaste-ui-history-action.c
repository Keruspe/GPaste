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

#include <gpaste-ui-history-action-private.h>

typedef struct
{
    GPasteClient *client;

    GtkWindow    *rootwin;

    gchar        *history;
} GPasteUiHistoryActionPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiHistoryAction, g_paste_ui_history_action, GTK_TYPE_BUTTON)

/**
 * g_paste_ui_history_action_set_history:
 * @self: a #GPasteUiHistoryAction instance
 * @history: the history to delete
 *
 * Set the history to delete
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_history_action_set_history (GPasteUiHistoryAction *self,
                                       const gchar           *history)
{
    g_return_if_fail (G_PASTE_IS_UI_HISTORY_ACTION (self));

    GPasteUiHistoryActionPrivate *priv = g_paste_ui_history_action_get_instance_private (G_PASTE_UI_HISTORY_ACTION (self));

    g_free (priv->history);
    priv->history = g_strdup (history);
}

static gboolean
g_paste_ui_history_action_button_press_event (GtkWidget      *widget,
                                              GdkEventButton *event G_GNUC_UNUSED)
{
    GPasteUiHistoryAction *self = G_PASTE_UI_HISTORY_ACTION (widget);
    GPasteUiHistoryActionClass *klass = G_PASTE_UI_HISTORY_ACTION_GET_CLASS (self);
    GPasteUiHistoryActionPrivate *priv = g_paste_ui_history_action_get_instance_private (self);

    if (priv->history && klass->activate)
        return klass->activate (self, priv->client, priv->rootwin, priv->history);
    else
        return GTK_WIDGET_CLASS (g_paste_ui_history_action_parent_class)->button_press_event (widget, event);
}

static void
g_paste_ui_history_action_dispose (GObject *object)
{
    GPasteUiHistoryActionPrivate *priv = g_paste_ui_history_action_get_instance_private (G_PASTE_UI_HISTORY_ACTION (object));

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_history_action_parent_class)->dispose (object);
}

static void
g_paste_ui_history_action_finalize (GObject *object)
{
    GPasteUiHistoryActionPrivate *priv = g_paste_ui_history_action_get_instance_private (G_PASTE_UI_HISTORY_ACTION (object));

    g_free (priv->history);

    G_OBJECT_CLASS (g_paste_ui_history_action_parent_class)->finalize (object);
}

static void
g_paste_ui_history_action_class_init (GPasteUiHistoryActionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_ui_history_action_dispose;
    object_class->finalize = g_paste_ui_history_action_finalize;

    GTK_WIDGET_CLASS (klass)->button_press_event = g_paste_ui_history_action_button_press_event;
}

static void
g_paste_ui_history_action_init (GPasteUiHistoryAction *self)
{
    GtkWidget *button = GTK_WIDGET (self);

    gtk_widget_set_margin_start (button, 5);
    gtk_widget_set_margin_end (button, 5);
}

/**
 * g_paste_ui_history_action_new: (skip)
 */
GtkWidget *
g_paste_ui_history_action_new (GType         type,
                               GPasteClient *client,
                               GtkWindow    *rootwin)
{
    g_return_val_if_fail (g_type_is_a (type, G_PASTE_TYPE_UI_HISTORY_ACTION), NULL);
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = gtk_widget_new (type,
                                      "width-request",  200,
                                      "height-request", 30,
                                      NULL);
    GPasteUiHistoryActionPrivate *priv = g_paste_ui_history_action_get_instance_private (G_PASTE_UI_HISTORY_ACTION (self));

    priv->client = g_object_ref (client);
    priv->rootwin = rootwin;

    return self;
}
