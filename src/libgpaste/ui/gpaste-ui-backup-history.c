/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-backup-history.h>
#include <gpaste-ui-history-actions.h>
#include <gpaste-util.h>

#include "gpaste-gtk-compat.h"

struct _GPasteUiBackupHistory
{
    GPasteUiHistoryAction parent_instance;
};

G_PASTE_DEFINE_TYPE (UiBackupHistory, ui_backup_history, G_PASTE_TYPE_UI_HISTORY_ACTION)

enum
{
    C_ACTIVATED,

    C_LAST_SIGNAL
};

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
    g_autofree gchar *default_name = g_strdup_printf ("%s_backup", history);
    GtkWidget *dialog = gtk_dialog_new_with_buttons (PACKAGE_STRING, parent,
                                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_USE_HEADER_BAR,
                                                     _("Backup"), GTK_RESPONSE_OK,
                                                     _("Cancel"), GTK_RESPONSE_CANCEL,
                                                     NULL);
    GtkDialog *d = GTK_DIALOG (dialog);
    GtkWidget *label = gtk_label_new (_("Under which name do you want to backup this history?"));
    GtkWidget *entry = gtk_entry_new ();
    GtkEntry *e = GTK_ENTRY (entry);
    GtkWidget *vbox = gtk_dialog_get_content_area (d);
    GtkBox *box = GTK_BOX (vbox);

    gtk_widget_set_margin_start (vbox, 2);
    gtk_widget_set_margin_end (vbox, 2);
    gtk_widget_set_margin_bottom (vbox, 2);

    gtk_widget_set_vexpand (label, TRUE);
    gtk_widget_set_valign (label, TRUE);
    gtk_box_pack_start (box, label, TRUE, TRUE);
    gtk_widget_show (label);

    gtk_widget_set_vexpand (entry, TRUE);
    gtk_widget_set_valign (entry, TRUE);
    gtk_box_pack_start (box, entry, TRUE, TRUE);
    gtk_entry_set_text (e, default_name);
    gtk_widget_show (entry);

    guint64 c_signals[C_LAST_SIGNAL] = {
       [C_ACTIVATED] = g_signal_connect (G_OBJECT (entry),
                                         "activate",
                                         G_CALLBACK (on_entry_activated),
                                         dialog)
    };

    gchar *backup = NULL;

    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK)
    {
        const gchar *text = gtk_entry_get_text (e);

        if (text && *text)
            backup = g_strdup (text);
    }

    g_signal_handler_disconnect (entry, c_signals[C_ACTIVATED]);
    gtk_widget_destroy (dialog);

    return backup;
}

static gboolean
g_paste_ui_backup_history_activate (GPasteUiHistoryAction *self G_GNUC_UNUSED,
                                    GPasteClient          *client,
                                    GPasteSettings        *settings G_GNUC_UNUSED,
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
 * @settings: a #GPasteSettings
 * @actions: the #GPasteUiHistoryActions
 * @rootwin: the root #GtkWindow
 *
 * Create a new instance of #GPasteUiBackupHistory
 *
 * Returns: a newly allocated #GPasteUiBackupHistory
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_backup_history_new (GPasteClient   *client,
                               GPasteSettings *settings,
                               GtkWidget      *actions,
                               GtkWindow      *rootwin)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (_G_PASTE_IS_UI_HISTORY_ACTIONS (actions), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    return g_paste_ui_history_action_new (G_PASTE_TYPE_UI_BACKUP_HISTORY, client, settings, actions, rootwin, _("Backup"));
}
