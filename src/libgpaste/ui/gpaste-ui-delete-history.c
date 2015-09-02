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
#include <gpaste-ui-history-action-private.h>
#include <gpaste-util.h>

struct _GPasteUiDeleteHistory
{
    GPasteUiHistoryAction parent_instance;
};

G_DEFINE_TYPE (GPasteUiDeleteHistory, g_paste_ui_delete_history, GTK_TYPE_BUTTON)

static gboolean
g_paste_ui_delete_history_button_press_event (GtkWidget      *widget,
                                              GdkEventButton *event G_GNUC_UNUSED)
{
    GPasteUiHistoryActionPrivate *priv = g_paste_ui_history_action_get_private (G_PASTE_UI_HISTORY_ACTION (widget));

    if (priv->history && g_paste_util_confirm_dialog (priv->rootwin, _("Are you sure you want to delete this history?")))
        g_paste_client_delete_history (priv->client, priv->history, NULL, NULL);

    return TRUE;
}

static void
g_paste_ui_delete_history_class_init (GPasteUiDeleteHistoryClass *klass)
{
    GTK_WIDGET_CLASS (klass)->button_press_event = g_paste_ui_delete_history_button_press_event;
}

static void
g_paste_ui_delete_history_init (GPasteUiDeleteHistory *self)
{
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

    return g_paste_ui_history_action_new (G_PASTE_TYPE_UI_DELETE_HISTORY, client, rootwin);
}
