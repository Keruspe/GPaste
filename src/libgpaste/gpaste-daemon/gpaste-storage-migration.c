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

#ifdef G_PASTE_ENABLE_PWQUALITY
#include <pwquality.h>
#endif

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

    /* The backend the history currently lives in (detected from the files on
     * disk): what we import from and clean up. */
    GPasteStorage                  current;

    /* The backends offered by the combo, in display order. */
    GPasteStorage                  backends[G_PASTE_N_STORAGE];
    guint                          n_backends;

    gboolean                       applied;
} MigrationData;

/* Work out which backend the history currently lives in by looking at the files
 * on disk, independent of the (possibly stale or unset) settings value. The
 * active history is checked first; failing that, whichever flavour has the most
 * files on disk wins, so an existing setup is still recognised. */
static GPasteStorage
detect_current_backend (GPasteSettings *settings)
{
    const gchar *name = g_paste_settings_get_history_name (settings);

#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_autoptr (GFile) encrypted = g_paste_util_get_history_file (name, "xmls");

    if (g_file_query_exists (encrypted, NULL))
        return G_PASTE_STORAGE_ENCRYPTED_FILE;
#endif

    g_autoptr (GFile) plain = g_paste_util_get_history_file (name, "xml");

    if (g_file_query_exists (plain, NULL))
        return G_PASTE_STORAGE_FILE;

    /* No history under the active name: fall back to the more-used flavour. */
    guint plain_count = 0;
#ifdef G_PASTE_ENABLE_ENCRYPTION
    guint encrypted_count = 0;
#endif
    g_autoptr (GFile) dir = g_paste_util_get_history_dir ();
    g_autoptr (GFileEnumerator) children = g_file_enumerate_children (dir,
                                                                      G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                                      G_FILE_QUERY_INFO_NONE,
                                                                      NULL, NULL);

    if (children)
    {
        GFileInfo *info;

        while ((info = g_file_enumerator_next_file (children, NULL, NULL)))
        {
            g_autoptr (GFileInfo) child = info;
            /* ".xmls" never matches the ".xml" suffix, so order does not matter. */
            const gchar *child_name = g_file_info_get_name (child);

            if (g_str_has_suffix (child_name, ".xml"))
                ++plain_count;
#ifdef G_PASTE_ENABLE_ENCRYPTION
            else if (g_str_has_suffix (child_name, ".xmls"))
                ++encrypted_count;
#endif
        }
    }

#ifdef G_PASTE_ENABLE_ENCRYPTION
    if (encrypted_count > 0 && encrypted_count >= plain_count)
        return G_PASTE_STORAGE_ENCRYPTED_FILE;
#endif

    if (plain_count > 0)
        return G_PASTE_STORAGE_FILE;

    return G_PASTE_STORAGE_NOOP;
}

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

G_PASTE_VISIBLE gboolean
g_paste_storage_migration_needed (GPasteSettings *settings)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), FALSE);

    return g_paste_settings_get_storage_backend_revision (settings) != G_PASTE_STORAGE_BACKEND_REVISION;
}

/* "Import" only makes sense when we are moving from a backend that has stored
 * data into a different one that also stores something; copying file -> file,
 * importing into "no storage", or importing from an empty "no storage" source
 * are all pointless, so the toggle is disabled in those cases. */
static gboolean
can_import (MigrationData *self,
            GPasteStorage  chosen)
{
    return self->current != G_PASTE_STORAGE_NOOP &&
           chosen != G_PASTE_STORAGE_NOOP &&
           chosen != self->current;
}

static void
update_state (MigrationData *self)
{
    GPasteStorage chosen = backend_for_index (self, adw_combo_row_get_selected (self->backend_row));
    /* There is only "old data" to delete once we actually leave a backend that
     * stored something; otherwise deleting it would throw away what we kept. */
    gboolean backend_changes = self->current != G_PASTE_STORAGE_NOOP && chosen != self->current;
    gboolean import_possible = can_import (self, chosen);

    gtk_widget_set_sensitive (GTK_WIDGET (self->import_row), import_possible);
    if (!import_possible)
        adw_switch_row_set_active (self->import_row, FALSE);

    gtk_widget_set_sensitive (GTK_WIDGET (self->cleanup_row), backend_changes);
    if (!backend_changes)
        adw_switch_row_set_active (self->cleanup_row, FALSE);

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

/* AdwComboRow ellipsizes the backend labels — both the dropdown rows and the
 * GtkInscription previewing the current selection — and offers no property to
 * stop it. Give it our own factory of plain GtkLabels instead, which it uses for
 * the dropdown rows and the selected-value preview alike, so the longer backend
 * descriptions show in full. */
static void
backend_label_setup (GtkSignalListItemFactory *factory G_GNUC_UNUSED,
                     GtkListItem              *item,
                     gpointer                  user_data G_GNUC_UNUSED)
{
    GtkWidget *label = gtk_label_new (NULL);

    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_list_item_set_child (item, label);
}

static void
backend_label_bind (GtkSignalListItemFactory *factory G_GNUC_UNUSED,
                    GtkListItem              *item,
                    gpointer                  user_data G_GNUC_UNUSED)
{
    GtkStringObject *string = gtk_list_item_get_item (item);

    gtk_label_set_label (GTK_LABEL (gtk_list_item_get_child (item)),
                         gtk_string_object_get_string (string));
}

/* Returns TRUE only if every history was copied into @chosen and reads back with
 * the expected size, so the caller never deletes the originals on a failed write
 * (e.g. an encrypted write that ran out of memory deriving the key). */
static gboolean
import_histories (GPasteSettings *settings,
                  GPasteStorage   current,
                  GPasteStorage   chosen)
{
    g_autoptr (GPasteStorageBackend) previous = g_paste_storage_backend_new (current, settings);
    g_autoptr (GPasteStorageBackend) next = g_paste_storage_backend_new (chosen, settings);
    g_auto (GStrv) names = g_paste_storage_backend_list_histories (previous, NULL);
    gboolean ok = TRUE;

    for (GStrv name = names; name && *name; ++name)
    {
        GList *history = NULL;
        gsize size = 0;

        g_paste_storage_backend_read_history (previous, *name, &history, &size);
        g_paste_storage_backend_write_history (next, *name, history);
        g_list_free_full (history, g_object_unref);

        GList *written = NULL;
        gsize written_size = 0;

        g_paste_storage_backend_read_history (next, *name, &written, &written_size);
        g_list_free_full (written, g_object_unref);

        if (written_size != size)
            ok = FALSE;
    }

    return ok;
}

static void
cleanup_histories (GPasteSettings *settings,
                   GPasteStorage   current)
{
    g_autoptr (GPasteStorageBackend) previous = g_paste_storage_backend_new (current, settings);
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

    /* Only delete the old data once the import (if any) is confirmed, so a failed
     * import can never wipe the history it was meant to migrate. */
    gboolean imported = TRUE;

    if (import && can_import (self, chosen))
        imported = import_histories (self->settings, self->current, chosen);

    if (cleanup && imported)
        cleanup_histories (self->settings, self->current);
    else if (cleanup)
        g_warning ("History import failed; keeping the old data instead of deleting it");

    g_paste_settings_set_storage_backend_revision (self->settings, G_PASTE_STORAGE_BACKEND_REVISION);

    /* Destroying the window frees self (its data); grab the callback first. */
    GPasteStorageMigrationDoneFunc done = self->done;
    gpointer done_data = self->user_data;

    gtk_window_destroy (self->window);

    if (done)
        done (done_data);
}

#ifdef G_PASTE_ENABLE_LIBSECRET
/* Try the passphrase remembered in the keyring, discarding it when it has gone
 * stale and no longer decrypts the history (so a stale entry never gets used).
 * Returns %TRUE when a usable passphrase is now set. */
static gboolean
try_keyring_passphrase (GPasteSettings *settings)
{
    if (!g_paste_storage_keyring_apply ())
        return FALSE;

    if (!g_paste_file_backend_passphrase_can_decrypt (settings, g_paste_storage_backend_get_passphrase ()))
    {
        g_warning ("The passphrase stored in the keyring does not unlock the history; asking for it");
        g_paste_storage_backend_set_passphrase (NULL);
        return FALSE;
    }

    return TRUE;
}
#endif

#ifdef G_PASTE_ENABLE_ENCRYPTION
typedef void (*UnlockDoneFunc) (gpointer user_data);

typedef struct
{
    GtkApplication *application;
    GPasteSettings *settings;
    UnlockDoneFunc  done;
    gpointer        user_data;
} UnlockPrompt;

static void
on_unlock_reply (const gchar *passphrase,
                 gpointer     user_data)
{
    UnlockPrompt *prompt = user_data;

    /* A wrong passphrase would load an empty history and let the next save
     * overwrite the real data, so never accept one that does not decrypt: ask
     * again instead. */
    if (passphrase && !g_paste_file_backend_passphrase_can_decrypt (prompt->settings, passphrase))
    {
        g_paste_storage_migration_prompt_passphrase (prompt->application, FALSE,
                                                     _("Wrong passphrase, please try again"),
                                                     on_unlock_reply, prompt);
        return;
    }

    /* NULL on dismissal: leave no passphrase set. */
    if (passphrase)
        g_paste_storage_backend_set_passphrase (passphrase);

    UnlockDoneFunc done = prompt->done;
    gpointer done_data = prompt->user_data;

    g_free (prompt);

    if (done)
        done (done_data);
}

/* Shared "unlock an existing encrypted history" prompt: ask for the passphrase,
 * verify it actually decrypts, re-prompt on a wrong one, then call @done once
 * settled (the passphrase is set on success, left unset on dismissal). Callers
 * try the keyring first; this is the prompt half they fall back to. */
static void
unlock_prompt (GtkApplication *application,
               GPasteSettings *settings,
               UnlockDoneFunc  done,
               gpointer        user_data)
{
    UnlockPrompt *prompt = g_new0 (UnlockPrompt, 1);

    prompt->application = application;
    prompt->settings = settings;
    prompt->done = done;
    prompt->user_data = user_data;

    g_paste_storage_migration_prompt_passphrase (application, FALSE, NULL, on_unlock_reply, prompt);
}

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

/* Once the source encrypted history is unlocked, apply the migration with the
 * chosen target; on dismissal (no passphrase) stay on the dialog instead. */
static void
on_source_unlocked (gpointer user_data)
{
    MigrationData *self = user_data;

    if (!g_paste_storage_backend_get_passphrase ())
        return;

    apply_migration (self, backend_for_index (self, adw_combo_row_get_selected (self->backend_row)));
}
#endif

static void
on_apply (GtkButton *button G_GNUC_UNUSED,
          gpointer   user_data)
{
    MigrationData *self = user_data;
    GPasteStorage chosen = backend_for_index (self, adw_combo_row_get_selected (self->backend_row));

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* Switching to encrypted storage needs a (new) passphrase to store with.
     * Keeping the existing encrypted backend does not: its passphrase is obtained
     * later through the daemon's normal unlock flow. */
    if (chosen == G_PASTE_STORAGE_ENCRYPTED_FILE && chosen != self->current &&
        !g_paste_storage_backend_get_passphrase ())
    {
        g_paste_storage_migration_prompt_passphrase (self->application, TRUE, NULL, on_passphrase_set, self);
        return;
    }

    /* Importing from (or deleting) an existing encrypted history needs its
     * passphrase to read or list it. Prefer one remembered in the keyring, and
     * only prompt when there is none or it has gone stale. */
    if (self->current == G_PASTE_STORAGE_ENCRYPTED_FILE && !g_paste_storage_backend_get_passphrase () &&
        (adw_switch_row_get_active (self->import_row) || adw_switch_row_get_active (self->cleanup_row)))
    {
#ifdef G_PASTE_ENABLE_LIBSECRET
        if (!try_keyring_passphrase (self->settings))
#endif
        {
            unlock_prompt (self->application, self->settings, on_source_unlocked, self);
            return;
        }
    }
#endif

    apply_migration (self, chosen);
}

/* Dismissing the dialog leaves the revision untouched so it is shown again on
 * the next start: the user has to make a deliberate choice. The detected current
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

/* Escape dismisses the dialog just like the window's close button: route it
 * through gtk_window_close() (which emits "close-request" -> on_close_request),
 * not gtk_window_destroy(), so the dismissal path runs and the revision stays
 * untouched. */
static gboolean
on_key_pressed (GtkEventControllerKey *controller G_GNUC_UNUSED,
                guint                  keyval,
                guint                  keycode    G_GNUC_UNUSED,
                GdkModifierType        state      G_GNUC_UNUSED,
                gpointer               user_data)
{
    MigrationData *self = user_data;

    if (keyval != GDK_KEY_Escape)
        return GDK_EVENT_PROPAGATE;

    gtk_window_close (self->window);
    return GDK_EVENT_STOP;
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
G_PASTE_VISIBLE void
g_paste_storage_migration_show (GtkApplication                 *application,
                                GPasteSettings                 *settings,
                                GPasteStorageMigrationDoneFunc  done,
                                gpointer                        user_data)
{
    g_return_if_fail (GTK_IS_APPLICATION (application));
    g_return_if_fail (_G_PASTE_IS_SETTINGS (settings));

    /* Detect the backend the history currently lives in from the files on disk,
     * and apply it right away so this session keeps the right backend even if the
     * dialog is dismissed without an explicit choice (importantly, an encrypted
     * history is not silently downgraded to "none"). */
    GPasteStorage current = detect_current_backend (settings);

    g_paste_settings_set_storage_backend (settings, current);

    MigrationData *self = g_new0 (MigrationData, 1);
    self->settings = g_object_ref (settings);
    self->application = application;
    self->done = done;
    self->user_data = user_data;
    self->current = current;

    GtkWidget *window = adw_application_window_new (application);
    self->window = GTK_WINDOW (window);
    gtk_window_set_title (self->window, _("Storage migration"));
    gtk_window_set_icon_name (self->window, G_PASTE_ICON_NAME);
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

    adw_combo_row_set_selected (self->backend_row, index_for_backend (self, current));

    GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
    g_signal_connect (factory, "setup", G_CALLBACK (backend_label_setup), NULL);
    g_signal_connect (factory, "bind", G_CALLBACK (backend_label_bind), NULL);
    adw_combo_row_set_factory (self->backend_row, factory);
    g_object_unref (factory);

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

    GtkEventController *key_controller = gtk_event_controller_key_new ();
    g_signal_connect (key_controller, "key-pressed", G_CALLBACK (on_key_pressed), self);
    gtk_widget_add_controller (window, key_controller);

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

    /* Only built when setting a new passphrase (confirm) and libpwquality is
     * available: the strength meter, the row carrying its rating/hint, and the
     * pwquality settings used to score the passphrase. */
    GtkLevelBar                *strength;
    GtkWidget                  *strength_row;
#ifdef G_PASTE_ENABLE_PWQUALITY
    pwquality_settings_t       *pwq;
#endif

    gboolean                    delivered;
} PassphraseData;

static void
passphrase_data_free (gpointer data)
{
    PassphraseData *self = data;

#ifdef G_PASTE_ENABLE_PWQUALITY
    if (self->pwq)
        pwquality_free_settings (self->pwq);
#endif

    g_free (self);
}

static const gchar *
passphrase_text (GtkEditable *editable)
{
    return editable ? gtk_editable_get_text (editable) : "";
}

#ifdef G_PASTE_ENABLE_PWQUALITY
/* The textual rating shown when the passphrase passes the basic checks (so
 * libpwquality has no specific complaint to surface instead). */
static const gchar *
passphrase_rating (guint level)
{
    switch (level)
    {
    case 1:
        return _("Weak");
    case 2:
        return _("Fair");
    case 3:
        return _("Good");
    case 4:
        return _("Strong");
    default:
        return "";
    }
}

/* GNOME-style passphrase rating via libpwquality (as gnome-control-center does):
 * map the 0-100 score to a 0-4 meter level and produce an actionable hint. On a
 * hard failure (too short, dictionary word, ...) libpwquality returns a negative
 * code whose localized reason becomes the hint. Returns the meter level and, in
 * @hint, a newly-allocated message to show (rating word or pwquality reason). */
static guint
passphrase_strength (pwquality_settings_t *pwq,
                     const gchar          *passphrase,
                     gchar               **hint)
{
    if (!passphrase || !*passphrase)
    {
        *hint = NULL;
        return 0;
    }

    void *auxerror = NULL;
    gint score = pwquality_check (pwq, passphrase, NULL, NULL, &auxerror);

    if (score < 0)
    {
        /* pwquality_strerror also consumes auxerror, so this frees it too. */
        gchar buf[PWQ_MAX_ERROR_MESSAGE_LEN];

        *hint = g_strdup (pwquality_strerror (buf, sizeof (buf), score, auxerror));
        return 1;
    }

    guint level = (score < 50) ? 1
                : (score < 75) ? 2
                : (score < 90) ? 3
                :                4;

    *hint = g_strdup (passphrase_rating (level));

    return level;
}
#endif

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
    PassphraseData *self = user_data;

    /* The red hint flags the previous wrong attempt; clear it as soon as the
     * user amends the passphrase so it does not bleed into the next try. */
    gtk_widget_remove_css_class (GTK_WIDGET (self->entry), "error");

#ifdef G_PASTE_ENABLE_PWQUALITY
    /* Reflect the strength of the new passphrase as it is typed. */
    if (self->strength)
    {
        g_autofree gchar *hint = NULL;
        guint strength = passphrase_strength (self->pwq, passphrase_text (self->entry), &hint);

        gtk_level_bar_set_value (self->strength, strength);
        adw_action_row_set_subtitle (ADW_ACTION_ROW (self->strength_row), hint ? hint : "");
    }
#endif

    passphrase_update_ok (self);
}

static void
passphrase_deliver (PassphraseData *self,
                    const gchar    *passphrase)
{
    if (self->delivered)
        return;

    self->delivered = TRUE;

    /* An empty passphrase is no passphrase: deliver NULL so callers treat it as
     * a dismissal rather than configuring an unprotected "encrypted" history
     * (they only null-check the pointer, not its contents). */
    if (passphrase && !*passphrase)
        passphrase = NULL;

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

/* Escape dismisses the prompt like the close button: gtk_window_close() emits
 * "close-request" (on_passphrase_close), which delivers NULL. */
static gboolean
on_passphrase_key_pressed (GtkEventControllerKey *controller G_GNUC_UNUSED,
                           guint                  keyval,
                           guint                  keycode    G_GNUC_UNUSED,
                           GdkModifierType        state      G_GNUC_UNUSED,
                           gpointer               user_data)
{
    PassphraseData *self = user_data;

    if (keyval != GDK_KEY_Escape)
        return GDK_EVENT_PROPAGATE;

    gtk_window_close (self->window);
    return GDK_EVENT_STOP;
}

/**
 * g_paste_storage_migration_prompt_passphrase:
 * @application: the #GtkApplication to anchor the dialog to
 * @confirm: whether to ask for the passphrase twice (new encrypted history)
 * @error_message: (nullable): an error to show above the entry (e.g. when
 *                 re-prompting after a wrong passphrase)
 * @done: (scope async): receives the entered passphrase, or %NULL if dismissed
 * @user_data: data passed to @done
 *
 * Prompt the user for the encrypted history passphrase.
 */
G_PASTE_VISIBLE void
g_paste_storage_migration_prompt_passphrase (GtkApplication              *application,
                                             gboolean                     confirm,
                                             const gchar                 *error_message,
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
    gtk_window_set_icon_name (self->window, G_PASTE_ICON_NAME);
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

    /* Re-prompt after a wrong passphrase: flag the entry and say what went wrong. */
    if (error_message)
    {
        adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (group), error_message);
        gtk_widget_add_css_class (entry, "error");
    }

    if (confirm)
    {
        GtkWidget *confirm_entry = adw_password_entry_row_new ();
        self->confirm_entry = GTK_EDITABLE (confirm_entry);
        adw_preferences_row_set_title (ADW_PREFERENCES_ROW (confirm_entry), _("Confirm passphrase"));
        g_signal_connect (confirm_entry, "changed", G_CALLBACK (on_passphrase_changed), self);
        adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), confirm_entry);

#ifdef G_PASTE_ENABLE_PWQUALITY
        /* Rate the new passphrase as it is typed (libpwquality), with the rating
         * or pwquality's advice as the subtitle and a colour-graded meter. */
        self->pwq = pwquality_default_settings ();
        pwquality_read_config (self->pwq, NULL, NULL);

        GtkWidget *strength_row = adw_action_row_new ();
        self->strength_row = strength_row;
        adw_preferences_row_set_title (ADW_PREFERENCES_ROW (strength_row), _("Passphrase strength"));

        GtkWidget *strength = gtk_level_bar_new ();
        self->strength = GTK_LEVEL_BAR (strength);
        gtk_level_bar_set_min_value (self->strength, 0);
        gtk_level_bar_set_max_value (self->strength, 4);
        /* Colour the meter red → orange → green as the rating climbs. */
        gtk_level_bar_add_offset_value (self->strength, GTK_LEVEL_BAR_OFFSET_LOW, 1);
        gtk_level_bar_add_offset_value (self->strength, GTK_LEVEL_BAR_OFFSET_HIGH, 3);
        gtk_level_bar_add_offset_value (self->strength, GTK_LEVEL_BAR_OFFSET_FULL, 4);
        gtk_widget_set_valign (strength, GTK_ALIGN_CENTER);
        gtk_widget_set_size_request (strength, 120, -1);
        adw_action_row_add_suffix (ADW_ACTION_ROW (strength_row), strength);
        adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), strength_row);
#endif

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
    g_object_set_data_full (G_OBJECT (window), "gpaste-passphrase-data", self, passphrase_data_free);

    GtkEventController *key_controller = gtk_event_controller_key_new ();
    g_signal_connect (key_controller, "key-pressed", G_CALLBACK (on_passphrase_key_pressed), self);
    gtk_widget_add_controller (window, key_controller);

    gtk_window_present (self->window);
}

G_PASTE_VISIBLE gboolean
g_paste_storage_decryption_needed (GPasteSettings *settings)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), FALSE);

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* Only an encrypted history that is not already unlocked needs decrypting. */
    if (g_paste_settings_get_storage_backend (settings) != G_PASTE_STORAGE_ENCRYPTED_FILE ||
        g_paste_storage_backend_get_passphrase ())
        return FALSE;

#ifdef G_PASTE_ENABLE_LIBSECRET
    /* A keyring passphrase that unlocks the history is applied here, so no prompt
     * (and, in gnome-shell, no helper) is needed: the caller can load straight
     * away with the passphrase now set in this process. */
    if (try_keyring_passphrase (settings))
        return FALSE;
#endif

    return TRUE;
#else
    return FALSE;
#endif
}

/**
 * g_paste_storage_decryption_show:
 * @application: a #GtkApplication instance
 * @settings: a #GPasteSettings instance
 * @done: (scope async) (nullable): called once the history is unlocked or the prompt dismissed
 * @user_data: data passed to @done
 *
 * Unlock an already-encrypted history through a passphrase prompt.
 */
G_PASTE_VISIBLE void
g_paste_storage_decryption_show (GtkApplication                 *application,
                                 GPasteSettings                 *settings,
                                 GPasteStorageMigrationDoneFunc  done,
                                 gpointer                        user_data)
{
    g_return_if_fail (GTK_IS_APPLICATION (application));
    g_return_if_fail (_G_PASTE_IS_SETTINGS (settings));

#ifdef G_PASTE_ENABLE_ENCRYPTION
    unlock_prompt (application, settings, done, user_data);
#else
    if (done)
        done (user_data);
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

G_PASTE_VISIBLE void
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
