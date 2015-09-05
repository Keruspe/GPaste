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

#include <gpaste-ui-backup-history.h>
#include <gpaste-ui-history-action-private.h>
#include <gpaste-ui-history-actions.h>
#include <gpaste-util.h>

struct _GPasteUiBackupHistory
{
    GPasteUiHistoryAction parent_instance;
};

G_DEFINE_TYPE (GPasteUiBackupHistory, g_paste_ui_backup_history, G_PASTE_TYPE_UI_HISTORY_ACTION)

static void
on_entry_activated (GtkEntry *entry G_GNUC_UNUSED,
                    gpointer  user_data)
{
    GtkDialog *dialog = user_data;

    gtk_dialog_response (dialog, GTK_RESPONSE_OK);
}

static gchar *
g_paste_ui_backup_history_confirm_dialog (GtkWindow   *parent,
                                          const gchar *history)
{
    g_return_val_if_fail (!parent || GTK_IS_WINDOW (parent), FALSE);

    GtkWidget *dialog = gtk_message_dialog_new (parent,
                                                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_USE_HEADER_BAR,
                                                GTK_MESSAGE_QUESTION,
                                                GTK_BUTTONS_OK_CANCEL,
                                                _("Under which name do you want to backup this history?"));
    GtkWidget *entry = gtk_entry_new ();
    GtkEntry *e = GTK_ENTRY (entry);
    g_autofree gchar *default_name = g_strdup_printf ("%s_backup", history);

    gtk_entry_set_text (e, default_name);
    gtk_container_add (GTK_CONTAINER (gtk_message_dialog_get_message_area (GTK_MESSAGE_DIALOG (dialog))), entry);
    gtk_widget_show (entry);

    gulong activated_id = g_signal_connect (G_OBJECT (entry),
                                            "activate",
                                            G_CALLBACK (on_entry_activated),
                                            dialog);

    gchar *backup = NULL;

    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK)
    {
        const gchar *text = gtk_entry_get_text (e);

        if (text && *text)
            backup = g_strdup (text);
    }

    g_signal_handler_disconnect (entry, activated_id);
    gtk_widget_destroy (dialog);

    return backup;
}

static gboolean
g_paste_ui_backup_history_activate (GPasteUiHistoryAction *self G_GNUC_UNUSED,
                                    GPasteClient          *client,
                                    GtkWindow             *rootwin,
                                    const gchar           *history)
{
    g_autofree gchar *backup = g_paste_ui_backup_history_confirm_dialog (rootwin, history);

    if (backup)
        g_paste_client_backup_history (client, history, backup, NULL, NULL);

    return TRUE;
}

static void
g_paste_ui_backup_history_class_init (GPasteUiBackupHistoryClass *klass)
{
    G_PASTE_UI_HISTORY_ACTION_CLASS (klass)->activate = g_paste_ui_backup_history_activate;
}

static void
g_paste_ui_backup_history_init (GPasteUiBackupHistory *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_ui_backup_history_new:
 * @client: a #GPasteClient
 * @actions: the #GPasteUiHistoryActions
 * @rootwin: the root #GtkWindow
 *
 * Create a new instance of #GPasteUiBackupHistory
 *
 * Returns: a newly allocated #GPasteUiBackupHistory
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_backup_history_new (GPasteClient *client,
                               GtkWidget    *actions,
                               GtkWindow    *rootwin)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (G_PASTE_IS_UI_HISTORY_ACTIONS (actions), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = g_paste_ui_history_action_new (G_PASTE_TYPE_UI_BACKUP_HISTORY, client, actions, rootwin);

    gtk_button_set_label (GTK_BUTTON (self), _("Backup"));

    return self;
}
