// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-pages.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-group.h>

typedef struct _DaemonRequest DaemonRequest;

typedef void (*DaemonCall)   (GPasteClient       *client,
                              GCancellable       *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer            user_data);
typedef void (*DaemonFinish) (GPasteClient *client,
                              GAsyncResult *result,
                              GError      **error);

/* What one of the storage buttons asked the daemon, for as long as it is out:
 * the button stays insensitive meanwhile, and the application is held, so that
 * closing the preferences does not end the process before the answer -- a
 * refused migration has its gate to close then, and either has a failure to
 * report. */
struct _DaemonRequest
{
    AdwButtonRow *row;
    GApplication *application;   /* held while out, or NULL */
    GPasteClient *client;
    DaemonCall    call;
    DaemonFinish  finish;
    const gchar  *failure_title; /* what a failure is shown under */
    const gchar  *failure_log;   /* what it is warned as, with no window to show it on */
    gboolean      migration;     /* whether it opens the migration gate */
    guint64       revision;      /* what closes the gate again */
};

/* A storage row is sensitive while it has no request out, and the passphrase
 * row -- the one carrying the settings -- only while the history is encrypted
 * too. A reply and a change of backend both move it, so both go through this
 * one rule, neither undoing what the other holds it to. */
static void
update_row_sensitivity (AdwButtonRow *row)
{
    GPasteSettings *settings = g_object_get_data (G_OBJECT (row), "gpaste-encrypted-only");
    gboolean requesting = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (row), "gpaste-requesting"));

    gtk_widget_set_sensitive (GTK_WIDGET (row),
                              !requesting && (!settings || g_paste_storage_is_encrypted (g_paste_settings_get_storage_backend (settings))));
}

static void
set_requesting (AdwButtonRow *row,
                gboolean      requesting)
{
    g_object_set_data (G_OBJECT (row), "gpaste-requesting", GINT_TO_POINTER (requesting));
    update_row_sensitivity (row);
}

static void
daemon_request_free (DaemonRequest *self)
{
    g_clear_object (&self->client);

    if (self->application)
        g_application_release (self->application);

    g_object_unref (self->row);
    g_free (self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DaemonRequest, daemon_request_free)

static DaemonRequest *
daemon_request_new (AdwButtonRow *row,
                    gboolean      migration)
{
    DaemonRequest *self = g_new0 (DaemonRequest, 1);

    self->row = g_object_ref (row);
    self->migration = migration;
    self->application = g_application_get_default ();
    if (self->application)
        g_application_hold (self->application);

    if (migration)
    {
        self->call = g_paste_client_reexecute;
        self->finish = g_paste_client_reexecute_finish;
        self->failure_title = _("Could Not Change Storage Backend");
        self->failure_log = "Could not change the storage backend";
    }
    else
    {
        self->call = g_paste_client_change_passphrase;
        self->finish = g_paste_client_change_passphrase_finish;
        self->failure_title = _("Could Not Change Passphrase");
        self->failure_log = "Could not change the passphrase";
    }

    set_requesting (row, TRUE);

    return self;
}

/* Consumes @self. */
static void
daemon_request_complete (DaemonRequest *self,
                         const GError  *error)
{
    g_autoptr (DaemonRequest) request = self;

    set_requesting (request->row, FALSE);

    if (!error)
        return;

    GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (request->row));

    /* The dialog needs a window to go on, and the preferences can be closed
     * while the daemon is still answering -- which is exactly when a request
     * that failed has nowhere to report itself. The journal is the only record
     * left then, so the failure is warned about rather than dropped
     * (/preferences/storage/refused-without-window). Where there is a window
     * the dialog is the report, and no warning goes out for something the user
     * was shown. */
    if (!root)
    {
        g_warning ("%s: %s", request->failure_log, error->message);
        return;
    }

    AdwAlertDialog *dialog = ADW_ALERT_DIALOG (adw_alert_dialog_new (request->failure_title, error->message));

    adw_alert_dialog_add_response (dialog, "close", _("Close"));
    adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (root));
}

static void
on_daemon_request_answered (GObject      *source,
                            GAsyncResult *result,
                            gpointer      user_data)
{
    DaemonRequest *self = user_data;
    g_autoptr (GError) error = NULL;

    self->finish (G_PASTE_CLIENT (source), result, &error);
    if (error && self->migration)
        g_paste_util_cancel_storage_migration (self->revision);
    daemon_request_complete (self, error);
}

static void
daemon_request_send (DaemonRequest *self)
{
    if (self->migration)
        self->revision = g_paste_util_prepare_storage_migration ();
    self->call (self->client, NULL, on_daemon_request_answered, self);
}

/* A daemon owning the name is not one serving the history yet -- one starting
 * behind its unlock prompt owns it before it exports anything -- and a call
 * reaching it then fails as a method it does not know, a migration's gate
 * closed again for nothing. So the request waits for one to serve
 * (g_paste_util_wait_for_daemon ()). */
static void
on_daemon_waited (GObject      *source G_GNUC_UNUSED,
                  GAsyncResult *result,
                  gpointer      user_data)
{
    DaemonRequest *self = user_data;
    g_autoptr (GError) error = NULL;

    if (g_paste_util_wait_for_daemon_finish (result, &error))
        daemon_request_send (self);
    else
        daemon_request_complete (self, error);
}

static void
on_daemon_request_client_ready (GObject      *source G_GNUC_UNUSED,
                                GAsyncResult *result,
                                gpointer      user_data)
{
    DaemonRequest *self = user_data;
    g_autoptr (GError) error = NULL;

    self->client = g_paste_client_new_finish (result, &error);

    if (!self->client)
    {
        daemon_request_complete (self, error);
        return;
    }

    g_paste_util_wait_for_daemon (self->client, NULL, on_daemon_waited, self);
}

static void
on_storage_migration_activated (AdwButtonRow *row,
                                gpointer      user_data G_GNUC_UNUSED)
{
    g_paste_client_new (on_daemon_request_client_ready, daemon_request_new (row, TRUE));
}

/* The daemon owns the prompts (it holds the current passphrase, and knows how
 * to raise them in its host), so it is asked rather than doing any of it here;
 * the passphrases never travel over the bus. */
static void
on_change_passphrase_activated (AdwButtonRow *row,
                                gpointer      user_data G_GNUC_UNUSED)
{
    g_paste_client_new (on_daemon_request_client_ready, daemon_request_new (row, FALSE));
}

static void
on_storage_backend_changed (GPasteSettings *settings G_GNUC_UNUSED,
                            GParamSpec     *pspec    G_GNUC_UNUSED,
                            gpointer        user_data)
{
    update_row_sensitivity (user_data);
}

/**
 * g_paste_gtk_preferences_history_settings_page_new:
 * @settings: a #GPasteSettings instance
 *
 * Build the preferences page
 *
 * Returns: (transfer full): a newly allocated #AdwPreferencesPage
 */
AdwPreferencesPage *
g_paste_gtk_preferences_history_settings_page_new (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    AdwPreferencesPage *self = ADW_PREFERENCES_PAGE (g_object_new (ADW_TYPE_PREFERENCES_PAGE,
                                                                   "name", "history-settings",
                                                                   "title", _("History Settings"),
                                                                   "icon-name", "preferences-other",
                                                                   NULL));

    GPasteGtkPreferencesGroup *group = g_paste_gtk_preferences_group_new (_("Resource Limits"));
    g_paste_gtk_preferences_group_add_range_setting (group,
                                                     _("Max History Size"),
                                                     G_PASTE_MAX_HISTORY_SIZE_SETTING,
                                                     5, 65535, 5,
                                                     settings);
    g_paste_gtk_preferences_group_add_range_setting (group,
                                                     _("Max Memory Usage (MB)"),
                                                     G_PASTE_MAX_MEMORY_USAGE_SETTING,
                                                     5, 16383, 5,
                                                     settings);
    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    group = g_paste_gtk_preferences_group_new (_("Text Limits"));
    g_paste_gtk_preferences_group_add_range_setting (group,
                                                     _("Min Text Item Length"),
                                                     G_PASTE_MIN_TEXT_ITEM_SIZE_SETTING,
                                                     1, 65535, 1,
                                                     settings);
    g_paste_gtk_preferences_group_add_range_setting (group,
                                                     _("Max Text Item Length"),
                                                     G_PASTE_MAX_TEXT_ITEM_SIZE_SETTING,
                                                     1, 2147483647, 1,
                                                     settings);
    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    group = g_paste_gtk_preferences_group_new (_("Display Settings"));
    g_paste_gtk_preferences_group_add_range_setting (group,
                                                     _("Max Element Size When Displaying"),
                                                     G_PASTE_ELEMENT_SIZE_SETTING,
                                                     0, 511, 5,
                                                     settings);
    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    group = g_paste_gtk_preferences_group_new (_("Storage"));
    adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (group),
                                           _("Choose how the history is stored on disk (plain or encrypted)."));
    g_paste_gtk_preferences_group_add_button (group,
                                              _("Change Storage Backend…"),
                                              G_CALLBACK (on_storage_migration_activated),
                                              NULL);

    /* Only an encrypted history has a passphrase to change, so the row stays
     * visible (it is how the feature is discovered) but insensitive otherwise.
     * Bound to the setting rather than read once: the button above can change
     * the backend from this very window. */
    AdwButtonRow *passphrase = g_paste_gtk_preferences_group_add_button (group,
                                                                          _("Change Passphrase…"),
                                                                          G_CALLBACK (on_change_passphrase_activated),
                                                                          NULL);

    g_object_set_data_full (G_OBJECT (passphrase), "gpaste-encrypted-only", g_object_ref (settings), g_object_unref);
    update_row_sensitivity (passphrase);
    g_signal_connect_object (settings, "notify::" G_PASTE_STORAGE_BACKEND_SETTING,
                             G_CALLBACK (on_storage_backend_changed), passphrase, 0);

    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    return self;
}
