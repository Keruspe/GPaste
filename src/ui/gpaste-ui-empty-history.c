/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-empty-history.h>
#include <gpaste-ui-history-actions.h>
#include <gpaste-util.h>

struct _GPasteUiEmptyHistory
{
    GPasteUiHistoryAction parent_instance;
};

G_PASTE_DEFINE_TYPE (UiEmptyHistory, ui_empty_history, G_PASTE_TYPE_UI_HISTORY_ACTION)

static gboolean
g_paste_ui_empty_history_activate (GPasteUiHistoryAction *self G_GNUC_UNUSED,
                                   GPasteClient          *client,
                                   GPasteSettings        *settings,
                                   GtkWindow             *rootwin,
                                   const gchar           *history)
{
    g_paste_util_empty_history (rootwin, client, settings, history);

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
 * g_paste_ui_empty_history_new:
 * @client: a #GPasteClient instance
 * @settings: a #GPasteSettings instance
 * @actions: the #GPasteUiHistoryActions
 * @rootwin: the main #GtkWindow
 *
 * Create a new instance of #GPasteUiEmptyHistory
 *
 * Returns: a newly allocated #GPasteUiEmptyHistory
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_empty_history_new (GPasteClient   *client,
                              GPasteSettings *settings,
                              GtkWidget      *actions,
                              GtkWindow      *rootwin)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (_G_PASTE_IS_UI_HISTORY_ACTIONS (actions), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    /* Translators: this is the translation for emptying the history */
    return g_paste_ui_history_action_new (G_PASTE_TYPE_UI_EMPTY_HISTORY, client, settings, actions, rootwin, _("Empty"));
}
