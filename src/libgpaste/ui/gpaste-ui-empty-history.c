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

#include <gpaste-ui-empty-history.h>
#include <gpaste-ui-history-actions.h>
#include <gpaste-util.h>

struct _GPasteUiEmptyHistory
{
    GPasteUiHistoryAction parent_instance;
};

G_DEFINE_TYPE (GPasteUiEmptyHistory, g_paste_ui_empty_history, G_PASTE_TYPE_UI_HISTORY_ACTION)

static gboolean
g_paste_ui_empty_history_activate (GPasteUiHistoryAction *self G_GNUC_UNUSED,
                                   GPasteClient          *client,
                                   GtkWindow             *rootwin,
                                   const gchar           *history)
{
    /* Translators: this is the translation for emptying the history */
    if (g_paste_util_confirm_dialog (rootwin, _("Empty"), _("Do you really want to empty the history?")))
        g_paste_client_empty_history (client, history, NULL, NULL);

    return TRUE;
}

static void
g_paste_ui_empty_history_class_init (GPasteUiEmptyHistoryClass *klass)
{
    G_PASTE_UI_HISTORY_ACTION_CLASS (klass)->activate = g_paste_ui_empty_history_activate;
}

static void
g_paste_ui_empty_history_init (GPasteUiEmptyHistory *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_ui_empty_new:
 * @client: a #GPasteClient instance
 * @actions: the #GPasteUiHistoryActions
 * @rootwin: the main #GtkWindow
 *
 * Create a new instance of #GPasteUiEmptyHistory
 *
 * Returns: a newly allocated #GPasteUiEmptyHistory
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_empty_history_new (GPasteClient *client,
                              GtkWidget    *actions,
                              GtkWindow    *rootwin)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (G_PASTE_IS_UI_HISTORY_ACTIONS (actions), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    /* Translators: this is the translation for emptying the history */
    return g_paste_ui_history_action_new (G_PASTE_TYPE_UI_EMPTY_HISTORY, client, actions, rootwin, _("Empty"));
}
