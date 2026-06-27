// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-util.h>

#include <gpaste-file-backend.h>
#include <gpaste-noop-backend.h>
#include <gpaste-storage-migration.h>

/* The backend the history used before this dialog ever ran: everything was
 * stored on disk, so "file" is what we import from and clean up. */
#define G_PASTE_STORAGE_PREVIOUS G_PASTE_STORAGE_FILE

/* The combo lists the backends in a fixed order; map both ways. */
static GPasteStorage
backend_for_index (guint index)
{
    return (index == 0) ? G_PASTE_STORAGE_FILE : G_PASTE_STORAGE_NOOP;
}

static guint
index_for_backend (GPasteStorage backend)
{
    return (backend == G_PASTE_STORAGE_NOOP) ? 1 : 0;
}

typedef struct
{
    GPasteSettings                *settings;
    GPasteStorageMigrationDoneFunc done;
    gpointer                       user_data;

    GtkWindow                     *window;
    AdwComboRow                   *backend_row;
    AdwSwitchRow                  *import_row;
    AdwSwitchRow                  *cleanup_row;
    AdwBanner                     *warning;

    gboolean                       applied;
} MigrationData;

static void
migration_data_free (gpointer data)
{
    MigrationData *self = data;

    g_object_unref (self->settings);
    g_free (self);
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
    GPasteStorage chosen = backend_for_index (adw_combo_row_get_selected (self->backend_row));
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
on_apply (GtkButton *button G_GNUC_UNUSED,
          gpointer   user_data)
{
    MigrationData *self = user_data;
    GPasteStorage chosen = backend_for_index (adw_combo_row_get_selected (self->backend_row));
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
    gtk_string_list_append (backends, _("Don't store anything"));

    GtkWidget *backend_row = adw_combo_row_new ();
    self->backend_row = ADW_COMBO_ROW (backend_row);
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (backend_row), _("Storage backend"));
    adw_combo_row_set_model (self->backend_row, G_LIST_MODEL (backends));
    g_object_unref (backends);
    adw_combo_row_set_selected (self->backend_row, index_for_backend (suggested));

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
