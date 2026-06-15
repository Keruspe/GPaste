// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-util.h>

#include <gpaste-daemon/gpaste-file-backend.h>
#include <gpaste-daemon/gpaste-noop-backend.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-storage-migration.h>

#ifdef G_PASTE_ENABLE_LIBSECRET
#include <gpaste-daemon/gpaste-storage-keyring.h>
#endif

/* The backend the history used before this dialog ever ran: everything was
 * stored on disk, so "file" is what we import from and clean up. */
#define G_PASTE_STORAGE_PREVIOUS G_PASTE_STORAGE_FILE

typedef struct
{
    GPasteSettings                *settings;
    GtkApplication                *application; /* to raise the passphrase prompt */
    GPasteStorageMigrationDoneFunc done;
    gpointer                       user_data;

    GtkWindow                     *window;
    AdwComboRow                   *backend_row;
    AdwSwitchRow                  *import_row;
    AdwSwitchRow                  *cleanup_row;
    AdwBanner                     *warning;

    /* The backends offered by the combo, in display order. */
    GPasteStorage                  backends[G_PASTE_N_STORAGE];
    guint                          n_backends;

    gboolean                       applied;
} MigrationData;

/* The combo lists the backends in a built-at-runtime order; map both ways. */
static GPasteStorage
backend_for_index (MigrationData *self,
                   guint          index)
{
    return (index < self->n_backends) ? self->backends[index] : G_PASTE_STORAGE_NOOP;
}

static guint
index_for_backend (MigrationData *self,
                   GPasteStorage  backend)
{
    for (guint i = 0; i < self->n_backends; ++i)
    {
        if (self->backends[i] == backend)
            return i;
    }

    return 0;
}

static void
migration_data_free (gpointer data)
{
    g_autofree MigrationData *self = data;

    g_object_unref (self->settings);
}

gboolean
g_paste_storage_migration_needed (GPasteSettings *settings)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), FALSE);

    return g_paste_settings_get_storage_backend_revision (settings) != G_PASTE_STORAGE_BACKEND_REVISION;
}

/* "Import" only makes sense when we are moving to a different backend that
 * actually stores something; copying file -> file or importing into "no
 * storage" is pointless, so the toggle is disabled in those cases. */
static gboolean
can_import (GPasteStorage chosen)
{
    return chosen != G_PASTE_STORAGE_NOOP && chosen != G_PASTE_STORAGE_PREVIOUS;
}

static void
update_state (MigrationData *self)
{
    GPasteStorage chosen = backend_for_index (self, adw_combo_row_get_selected (self->backend_row));
    gboolean import_possible = can_import (chosen);

    gtk_widget_set_sensitive (GTK_WIDGET (self->import_row), import_possible);
    if (!import_possible)
        adw_switch_row_set_active (self->import_row, FALSE);

    /* Deleting the old data without importing it first throws it away. */
    adw_banner_set_revealed (self->warning,
                             adw_switch_row_get_active (self->cleanup_row) &&
                             !adw_switch_row_get_active (self->import_row));
}

static void
on_state_changed (GObject    *object G_GNUC_UNUSED,
                  GParamSpec *pspec G_GNUC_UNUSED,
                  gpointer    user_data)
{
    update_state (user_data);
}

static void
import_histories (GPasteSettings *settings,
                  GPasteStorage   chosen)
{
    g_autoptr (GPasteStorageBackend) previous = g_paste_storage_backend_new (G_PASTE_STORAGE_PREVIOUS, settings);
    g_autoptr (GPasteStorageBackend) next = g_paste_storage_backend_new (chosen, settings);
    g_auto (GStrv) names = g_paste_storage_backend_list_histories (previous, NULL);

    for (GStrv name = names; name && *name; ++name)
    {
        GList *history = NULL;
        gsize size = 0;

        g_paste_storage_backend_read_history (previous, *name, &history, &size);
        g_paste_storage_backend_write_history (next, *name, history);
        g_list_free_full (history, g_object_unref);
    }
}

static void
cleanup_histories (GPasteSettings *settings)
{
    g_autoptr (GPasteStorageBackend) previous = g_paste_storage_backend_new (G_PASTE_STORAGE_PREVIOUS, settings);
    g_auto (GStrv) names = g_paste_storage_backend_list_histories (previous, NULL);

    for (GStrv name = names; name && *name; ++name)
        g_paste_storage_backend_delete_history (previous, *name, NULL);
}

static void
apply_migration (MigrationData *self,
                 GPasteStorage  chosen)
{
    gboolean import = adw_switch_row_get_active (self->import_row);
    gboolean cleanup = adw_switch_row_get_active (self->cleanup_row);

    self->applied = TRUE;

    g_paste_settings_set_storage_backend (self->settings, chosen);

    if (import && can_import (chosen))
        import_histories (self->settings, chosen);
    if (cleanup)
        cleanup_histories (self->settings);

    g_paste_settings_set_storage_backend_revision (self->settings, G_PASTE_STORAGE_BACKEND_REVISION);

    /* Destroying the window frees self (its data); grab the callback first. */
    GPasteStorageMigrationDoneFunc done = self->done;
    gpointer done_data = self->user_data;

    gtk_window_destroy (self->window);

    if (done)
        done (done_data);
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
static void
on_passphrase_set (const gchar *passphrase,
                   gpointer     user_data)
{
    MigrationData *self = user_data;

    /* Cancelled: stay on the migration dialog so another backend can be picked. */
    if (!passphrase)
        return;

    g_paste_storage_backend_set_passphrase (passphrase);
    apply_migration (self, G_PASTE_STORAGE_ENCRYPTED_FILE);
}
#endif

static void
on_apply (GtkButton *button G_GNUC_UNUSED,
          gpointer   user_data)
{
    MigrationData *self = user_data;
    GPasteStorage chosen = backend_for_index (self, adw_combo_row_get_selected (self->backend_row));

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* The encrypted backend needs a passphrase before it can import or store. */
    if (chosen == G_PASTE_STORAGE_ENCRYPTED_FILE && !g_paste_storage_backend_get_passphrase ())
    {
        g_paste_storage_migration_prompt_passphrase (self->application, TRUE, on_passphrase_set, self);
        return;
    }
#endif

    apply_migration (self, chosen);
}

/* Dismissing the dialog leaves the revision untouched so it is shown again on
 * the next start: the user has to make a deliberate choice. The suggested
 * backend (already written below) is used for this session in the meantime.
 * gtk_window_destroy() (the apply path) does not emit "close-request", so this
 * only runs for an actual dismissal. */
static gboolean
on_close_request (GtkWindow *window G_GNUC_UNUSED,
                  gpointer   user_data)
{
    MigrationData *self = user_data;

    if (!self->applied && self->done)
        self->done (self->user_data);

    return GDK_EVENT_PROPAGATE;
}

/**
 * g_paste_storage_migration_show:
 * @application: the #GtkApplication to anchor the dialog to
 * @settings: a #GPasteSettings instance
 * @done: (scope async) (nullable): called once the dialog is dismissed
 * @user_data: data passed to @done
 *
 * Show the storage migration dialog.
 */
void
g_paste_storage_migration_show (GtkApplication                 *application,
                                GPasteSettings                 *settings,
                                GPasteStorageMigrationDoneFunc  done,
                                gpointer                        user_data)
{
    g_return_if_fail (GTK_IS_APPLICATION (application));
    g_return_if_fail (_G_PASTE_IS_SETTINGS (settings));

    /* Suggest "file" when there is existing data to keep, "none" otherwise, and
     * apply it right away so this session has a backend even if the dialog is
     * dismissed without an explicit choice. */
    g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_auto (GStrv) histories = g_paste_storage_backend_list_histories (file, NULL);
    GPasteStorage suggested = (histories && histories[0]) ? G_PASTE_STORAGE_FILE : G_PASTE_STORAGE_NOOP;

    g_paste_settings_set_storage_backend (settings, suggested);

    MigrationData *self = g_new0 (MigrationData, 1);
    self->settings = g_object_ref (settings);
    self->application = application;
    self->done = done;
    self->user_data = user_data;

    GtkWidget *window = adw_application_window_new (application);
    self->window = GTK_WINDOW (window);
    gtk_window_set_title (self->window, _("Storage migration"));
    gtk_window_set_default_size (self->window, 480, -1);
    gtk_window_set_modal (self->window, TRUE);

    GtkWidget *apply = gtk_button_new_with_label (_("Apply"));
    gtk_widget_add_css_class (apply, "suggested-action");
    g_signal_connect (apply, "clicked", G_CALLBACK (on_apply), self);

    GtkWidget *header = adw_header_bar_new ();
    adw_header_bar_pack_end (ADW_HEADER_BAR (header), apply);

    GtkWidget *warning = adw_banner_new (_("The old data will be deleted without being imported first"));
    self->warning = ADW_BANNER (warning);

    GtkStringList *backends = gtk_string_list_new (NULL);

    gtk_string_list_append (backends, _("Store the history in a file"));
    self->backends[self->n_backends++] = G_PASTE_STORAGE_FILE;
#ifdef G_PASTE_ENABLE_ENCRYPTION
    gtk_string_list_append (backends, _("Store the history in an encrypted file"));
    self->backends[self->n_backends++] = G_PASTE_STORAGE_ENCRYPTED_FILE;
#endif
    gtk_string_list_append (backends, _("Don't store anything"));
    self->backends[self->n_backends++] = G_PASTE_STORAGE_NOOP;

    GtkWidget *backend_row = adw_combo_row_new ();
    self->backend_row = ADW_COMBO_ROW (backend_row);
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (backend_row), _("Storage backend"));
    adw_combo_row_set_model (self->backend_row, G_LIST_MODEL (backends));
    g_object_unref (backends);
    adw_combo_row_set_selected (self->backend_row, index_for_backend (self, suggested));

    GtkWidget *import_row = adw_switch_row_new ();
    self->import_row = ADW_SWITCH_ROW (import_row);
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (import_row), _("Import existing data"));
    adw_action_row_set_subtitle (ADW_ACTION_ROW (import_row), _("Copy the current history into the new backend"));

    GtkWidget *cleanup_row = adw_switch_row_new ();
    self->cleanup_row = ADW_SWITCH_ROW (cleanup_row);
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (cleanup_row), _("Delete old data afterwards"));
    adw_action_row_set_subtitle (ADW_ACTION_ROW (cleanup_row), _("Remove the previous on-disk history once done"));

    GtkWidget *group = adw_preferences_group_new ();
    adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (group),
                                           _("Choose where GPaste should store your clipboard history. "
                                             "Nothing is kept on disk unless you pick a storing backend here."));
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), backend_row);
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), import_row);
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), cleanup_row);

    GtkWidget *page = adw_preferences_page_new ();
    adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (group));

    GtkWidget *content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append (GTK_BOX (content), warning);
    gtk_box_append (GTK_BOX (content), page);

    GtkWidget *toolbar = adw_toolbar_view_new ();
    adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), header);
    adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), content);

    adw_application_window_set_content (ADW_APPLICATION_WINDOW (window), toolbar);

    g_signal_connect (backend_row, "notify::selected", G_CALLBACK (on_state_changed), self);
    g_signal_connect (import_row, "notify::active", G_CALLBACK (on_state_changed), self);
    g_signal_connect (cleanup_row, "notify::active", G_CALLBACK (on_state_changed), self);
    g_signal_connect (window, "close-request", G_CALLBACK (on_close_request), self);
    g_object_set_data_full (G_OBJECT (window), "gpaste-migration-data", self, migration_data_free);

    update_state (self);

    gtk_window_present (self->window);
}

typedef struct
{
    GPasteStoragePassphraseFunc done;
    gpointer                    user_data;
    gboolean                    confirm;

    GtkWindow                  *window;
    GtkEditable                *entry;
    GtkEditable                *confirm_entry;
    GtkWidget                  *remember;
    GtkWidget                  *ok;

    gboolean                    delivered;
} PassphraseData;

static const gchar *
passphrase_text (GtkEditable *editable)
{
    return editable ? gtk_editable_get_text (editable) : "";
}

static void
passphrase_update_ok (PassphraseData *self)
{
    const gchar *passphrase = passphrase_text (self->entry);
    gboolean ok = passphrase && *passphrase;

    /* When setting a new passphrase, both fields must match. */
    if (ok && self->confirm)
        ok = g_paste_str_equal (passphrase, passphrase_text (self->confirm_entry));

    gtk_widget_set_sensitive (self->ok, ok);
}

static void
on_passphrase_changed (GtkEditable *editable G_GNUC_UNUSED,
                       gpointer     user_data)
{
    passphrase_update_ok (user_data);
}

static void
passphrase_deliver (PassphraseData *self,
                    const gchar    *passphrase)
{
    if (self->delivered)
        return;

    self->delivered = TRUE;

    if (self->done)
        self->done (passphrase, self->user_data);
}

static void
on_passphrase_ok (GtkButton *button G_GNUC_UNUSED,
                  gpointer   user_data)
{
    PassphraseData *self = user_data;

#ifdef G_PASTE_ENABLE_LIBSECRET
    if (self->remember && adw_switch_row_get_active (ADW_SWITCH_ROW (self->remember)))
        g_paste_storage_keyring_store (passphrase_text (self->entry));
#endif

    /* Deliver while the entry text is still alive; the callback copies it into
     * secure memory. gtk_window_destroy() does not emit "close-request". */
    passphrase_deliver (self, passphrase_text (self->entry));
    gtk_window_destroy (self->window);
}

static gboolean
on_passphrase_close (GtkWindow *window G_GNUC_UNUSED,
                     gpointer   user_data)
{
    passphrase_deliver (user_data, NULL);

    return GDK_EVENT_PROPAGATE;
}

/**
 * g_paste_storage_migration_prompt_passphrase:
 * @application: the #GtkApplication to anchor the dialog to
 * @confirm: whether to ask for the passphrase twice (new encrypted history)
 * @done: (scope async): receives the entered passphrase, or %NULL if dismissed
 * @user_data: data passed to @done
 *
 * Prompt the user for the encrypted history passphrase.
 */
void
g_paste_storage_migration_prompt_passphrase (GtkApplication              *application,
                                             gboolean                     confirm,
                                             GPasteStoragePassphraseFunc  done,
                                             gpointer                     user_data)
{
    g_return_if_fail (GTK_IS_APPLICATION (application));

    PassphraseData *self = g_new0 (PassphraseData, 1);
    self->done = done;
    self->user_data = user_data;
    self->confirm = confirm;

    GtkWidget *window = adw_application_window_new (application);
    self->window = GTK_WINDOW (window);
    gtk_window_set_title (self->window, _("Encrypted history"));
    gtk_window_set_default_size (self->window, 420, -1);
    gtk_window_set_modal (self->window, TRUE);

    GtkWidget *ok = gtk_button_new_with_label (confirm ? _("Set passphrase") : _("Unlock"));
    self->ok = ok;
    gtk_widget_add_css_class (ok, "suggested-action");
    gtk_widget_set_sensitive (ok, FALSE);
    g_signal_connect (ok, "clicked", G_CALLBACK (on_passphrase_ok), self);

    GtkWidget *header = adw_header_bar_new ();
    adw_header_bar_pack_end (ADW_HEADER_BAR (header), ok);

    GtkWidget *entry = adw_password_entry_row_new ();
    self->entry = GTK_EDITABLE (entry);
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (entry), _("Passphrase"));
    g_signal_connect (entry, "changed", G_CALLBACK (on_passphrase_changed), self);

    GtkWidget *group = adw_preferences_group_new ();
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), entry);

    if (confirm)
    {
        GtkWidget *confirm_entry = adw_password_entry_row_new ();
        self->confirm_entry = GTK_EDITABLE (confirm_entry);
        adw_preferences_row_set_title (ADW_PREFERENCES_ROW (confirm_entry), _("Confirm passphrase"));
        g_signal_connect (confirm_entry, "changed", G_CALLBACK (on_passphrase_changed), self);
        adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), confirm_entry);
        adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (group),
                                               _("If you forget this passphrase, your stored history cannot be recovered."));
    }

#ifdef G_PASTE_ENABLE_LIBSECRET
    GtkWidget *remember = adw_switch_row_new ();
    self->remember = remember;
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (remember), _("Remember this passphrase"));
    adw_action_row_set_subtitle (ADW_ACTION_ROW (remember), _("Store it in the keyring so you are not asked again"));
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), remember);
#endif

    GtkWidget *page = adw_preferences_page_new ();
    adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (group));

    GtkWidget *toolbar = adw_toolbar_view_new ();
    adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), header);
    adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), page);
    adw_application_window_set_content (ADW_APPLICATION_WINDOW (window), toolbar);

    g_signal_connect (window, "close-request", G_CALLBACK (on_passphrase_close), self);
    g_object_set_data_full (G_OBJECT (window), "gpaste-passphrase-data", self, g_free);

    gtk_window_present (self->window);
}

static void
on_prepare_done (gpointer user_data)
{
    g_main_loop_quit (user_data);
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
static void
on_prepare_passphrase (const gchar *passphrase,
                       gpointer     user_data)
{
    if (passphrase)
        g_paste_storage_backend_set_passphrase (passphrase);

    g_main_loop_quit (user_data);
}
#endif

void
g_paste_storage_migration_prepare (GtkApplication *application,
                                   GPasteSettings *settings)
{
    g_return_if_fail (GTK_IS_APPLICATION (application));
    g_return_if_fail (_G_PASTE_IS_SETTINGS (settings));

    /* Also reachable later through the "storage-migration" action. */
    g_paste_storage_migration_register_action (application, settings);

    /* Let the user pick (or confirm) where the history is stored before the
     * daemon starts persisting anything. */
    if (g_paste_storage_migration_needed (settings))
    {
        g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);

        adw_init ();
        g_paste_storage_migration_show (application, settings, on_prepare_done, loop);
        g_main_loop_run (loop);
    }

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* An already-configured encrypted history needs to be unlocked before the
     * daemon loads it. */
    if (g_paste_settings_get_storage_backend (settings) == G_PASTE_STORAGE_ENCRYPTED_FILE &&
        !g_paste_storage_backend_get_passphrase ())
    {
#ifdef G_PASTE_ENABLE_LIBSECRET
        /* Prefer a passphrase remembered in the keyring over prompting. */
        g_paste_storage_keyring_apply ();
#endif

        if (!g_paste_storage_backend_get_passphrase ())
        {
            g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);

            adw_init ();
            g_paste_storage_migration_prompt_passphrase (application, FALSE, on_prepare_passphrase, loop);
            g_main_loop_run (loop);
        }
    }
#endif
}

static void
unref_settings (gpointer  data,
                GClosure *closure G_GNUC_UNUSED)
{
    g_object_unref (data);
}

static void
on_storage_migration_action (GSimpleAction *action G_GNUC_UNUSED,
                             GVariant      *parameter G_GNUC_UNUSED,
                             gpointer       user_data)
{
    GtkApplication *application = g_application_get_default () ? GTK_APPLICATION (g_application_get_default ()) : NULL;
    GPasteSettings *settings = user_data;

    if (application)
        g_paste_storage_migration_show (application, settings, NULL, NULL);
}

void
g_paste_storage_migration_register_action (GtkApplication *application,
                                           GPasteSettings *settings)
{
    g_return_if_fail (GTK_IS_APPLICATION (application));
    g_return_if_fail (_G_PASTE_IS_SETTINGS (settings));

    GSimpleAction *action = g_simple_action_new ("storage-migration", NULL);

    g_signal_connect_data (action, "activate",
                           G_CALLBACK (on_storage_migration_action),
                           g_object_ref (settings), unref_settings, 0);
    g_action_map_add_action (G_ACTION_MAP (application), G_ACTION (action));
    g_object_unref (action);
}
