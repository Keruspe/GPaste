/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-delete-history.h>
#include <gpaste-ui-history-actions.h>
#include <gpaste-util.h>

struct _GPasteUiDeleteHistory
{
    GPasteUiHistoryAction parent_instance;
};

G_PASTE_DEFINE_TYPE (UiDeleteHistory, ui_delete_history, G_PASTE_TYPE_UI_HISTORY_ACTION)

static gboolean
g_paste_ui_delete_history_activate (GPasteUiHistoryAction *self G_GNUC_UNUSED,
                                    GPasteClient          *client,
                                    GPasteSettings        *settings G_GNUC_UNUSED,
                                    GtkWindow             *rootwin,
                                    const gchar           *history)
{
    if (g_paste_util_confirm_dialog (rootwin, _("Delete"), _("Are you sure you want to delete this history?")))
        g_paste_client_delete_history (client, history, NULL, NULL);

    return TRUE;
}

static void
g_paste_ui_delete_history_class_init (GPasteUiDeleteHistoryClass *klass)
{
    G_PASTE_UI_HISTORY_ACTION_CLASS (klass)->activate = g_paste_ui_delete_history_activate;
}

static void
g_paste_ui_delete_history_init (GPasteUiDeleteHistory *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_ui_delete_history_new:
 * @client: a #GPasteClient
 * @settings: a #GPasteSettings
 * @actions: the #GPasteUiHistoryActions
 * @rootwin: the root #GtkWindow
 *
 * Create a new instance of #GPasteUiDeleteHistory
 *
 * Returns: a newly allocated #GPasteUiDeleteHistory
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_delete_history_new (GPasteClient   *client,
                               GPasteSettings *settings,
                               GtkWidget      *actions,
                               GtkWindow      *rootwin)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (_G_PASTE_IS_UI_HISTORY_ACTIONS (actions), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    return g_paste_ui_history_action_new (G_PASTE_TYPE_UI_DELETE_HISTORY, client, settings, actions, rootwin, _("Delete"));
}
