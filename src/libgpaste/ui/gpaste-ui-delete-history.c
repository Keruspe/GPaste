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

#include <gpaste-ui-delete-history.h>
#include <gpaste-util.h>

struct _GPasteUiDeleteHistory
{
    GtkButton parent_instance;
};

typedef struct
{
    GPasteClient *client;

    GtkWindow    *rootwin;

    gchar        *history;
} GPasteUiDeleteHistoryPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiDeleteHistory, g_paste_ui_delete_history, GTK_TYPE_BUTTON)

/**
 * g_paste_ui_delete_history_set_history:
 * @self: a #GPasteUiDeleteHistory instance
 * @history: the history to delete
 *
 * Set the history to delete
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_delete_history_set_history (GPasteUiDeleteHistory *self,
                                       const gchar           *history)
{
    g_return_if_fail (G_PASTE_IS_UI_DELETE_HISTORY (self));

    GPasteUiDeleteHistoryPrivate *priv = g_paste_ui_delete_history_get_instance_private (self);

    g_free (priv->history);
    priv->history = g_strdup (history);
}

static gboolean
g_paste_ui_delete_history_button_press_event (GtkWidget      *widget,
                                              GdkEventButton *event G_GNUC_UNUSED)
{
    GPasteUiDeleteHistoryPrivate *priv = g_paste_ui_delete_history_get_instance_private (G_PASTE_UI_DELETE_HISTORY (widget));

    if (priv->history && g_paste_util_confirm_dialog (priv->rootwin, _("Are you sure you want to delete this history?")))
        g_paste_client_delete_history (priv->client, priv->history, NULL, NULL);

    return TRUE;
}

static void
g_paste_ui_delete_history_dispose (GObject *object)
{
    GPasteUiDeleteHistoryPrivate *priv = g_paste_ui_delete_history_get_instance_private (G_PASTE_UI_DELETE_HISTORY (object));

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_delete_history_parent_class)->dispose (object);
}

static void
g_paste_ui_delete_history_finalize (GObject *object)
{
    GPasteUiDeleteHistoryPrivate *priv = g_paste_ui_delete_history_get_instance_private (G_PASTE_UI_DELETE_HISTORY (object));

    g_free (priv->history);

    G_OBJECT_CLASS (g_paste_ui_delete_history_parent_class)->finalize (object);
}

static void
g_paste_ui_delete_history_class_init (GPasteUiDeleteHistoryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_ui_delete_history_dispose;
    object_class->finalize = g_paste_ui_delete_history_finalize;
    GTK_WIDGET_CLASS (klass)->button_press_event = g_paste_ui_delete_history_button_press_event;
}

static void
g_paste_ui_delete_history_init (GPasteUiDeleteHistory *self)
{
    GtkWidget *button = GTK_WIDGET (self);

    gtk_widget_set_margin_start (button, 5);
    gtk_widget_set_margin_end (button, 5);

    gtk_button_set_label (GTK_BUTTON (self), _("Delete"));
}

/**
 * g_paste_ui_delete_history_new:
 * @client: a #GPasteClient
 * @rootwin: the root #GtkWindow
 *
 * Create a new instance of #GPasteUiDeleteHistory
 *
 * Returns: a newly allocated #GPasteUiDeleteHistory
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_delete_history_new (GPasteClient *client,
                               GtkWindow    *rootwin)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_DELETE_HISTORY, NULL);
    GPasteUiDeleteHistoryPrivate *priv = g_paste_ui_delete_history_get_instance_private (G_PASTE_UI_DELETE_HISTORY (self));

    priv->client = g_object_ref (client);
    priv->rootwin = rootwin;

    return self;
}
