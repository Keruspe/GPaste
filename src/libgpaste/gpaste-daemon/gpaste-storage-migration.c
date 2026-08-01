// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-util.h>

#include <gpaste-daemon/gpaste-file-backend.h>
#include <gpaste-daemon/gpaste-image-item.h>
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

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* The passphrase unlocking @current, once the keyring or the unlock prompt
     * has produced one that actually decrypts it. Kept here rather than in the
     * process-wide global because migrating between two encrypted flavors needs
     * the source key while the destination is being (re)keyed with another one.
     * Doubles as the "source already unlocked" flag, so a second Apply does not
     * ask again. In gcr secure memory; freed in migration_data_free(). */
    gchar                         *source_passphrase;

    /* Whether a new passphrase was asked for (only then is there a choice to
     * honour), and whether the user asked to remember it. Acted on once the
     * migration has actually applied, so a failed one never leaves the keyring
     * pointing at a history that was never written. */
    gboolean                       passphrase_prompted;
    GPasteStorageRemember          remember_passphrase;
#endif

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
    /* The storing flavours in tie-break precedence order (encrypted over plain,
     * file over database), used both for the active-name lookup and the on-disk
     * count fallback. Each kind's extension is the shared
     * g_paste_storage_get_extension(), so a new flavour only needs adding here. */
    static const GPasteStorage flavours[] = {
#ifdef G_PASTE_ENABLE_ENCRYPTION
        G_PASTE_STORAGE_ENCRYPTED_FILE,
#ifdef G_PASTE_ENABLE_SQLITE
        G_PASTE_STORAGE_ENCRYPTED_SQLITE,
#endif
#endif
#ifdef G_PASTE_ENABLE_SQLITE
        G_PASTE_STORAGE_SQLITE,
#endif
        G_PASTE_STORAGE_FILE,
    };

    const gchar *name = g_paste_settings_get_history_name (settings);

    /* The active history's own file wins, in the precedence order above, so an
     * existing encrypted setup is never demoted. */
    for (guint i = 0; i < G_N_ELEMENTS (flavours); ++i)
    {
        g_autoptr (GFile) file = g_paste_util_get_history_file (name, g_paste_storage_get_extension (flavours[i]));

        if (g_file_query_exists (file, NULL)) /* cancellable */
            return flavours[i];
    }

    /* No history under the active name: fall back to whichever flavour has the
     * most files on disk, ties broken by the same precedence. */
    g_autoptr (GStrvBuilder) suffix_builder = g_strv_builder_new ();
    guint counts[G_N_ELEMENTS (flavours)] = { 0 };

    for (guint i = 0; i < G_N_ELEMENTS (flavours); ++i)
        g_strv_builder_take (suffix_builder, g_strconcat (".", g_paste_storage_get_extension (flavours[i]), NULL));

    g_auto (GStrv) suffixes = g_strv_builder_end (suffix_builder);
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
            const gchar *child_name = g_file_info_get_name (child);

            /* The extensions are mutually non-suffixing (".xml" never matches
             * ".xmls", ".db" never ".dbs"), so each file counts for at most one. */
            for (guint i = 0; i < G_N_ELEMENTS (flavours); ++i)
            {
                if (g_str_has_suffix (child_name, suffixes[i]))
                {
                    ++counts[i];
                    break;
                }
            }
        }
    }

    /* Highest count wins; the array order breaks ties toward the higher-
     * precedence flavour (a later one must strictly exceed it). */
    guint best = 0;

    for (guint i = 1; i < G_N_ELEMENTS (flavours); ++i)
    {
        if (counts[i] > counts[best])
            best = i;
    }

    return counts[best] ? flavours[best] : G_PASTE_STORAGE_NOOP;
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

#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_paste_storage_passphrase_free (self->source_passphrase);
#endif

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

/* Whether @written faithfully reproduces @source: same kind, and same content
 * — an image's identity is its checksum (its value is a per-backend cache
 * path), everything else compares by real value. This deliberately differs from
 * g_paste_item_equals(): that dedup predicate ignores the kind and treats two
 * distinct passwords as never equal, whereas migration must confirm a password's
 * real value round-tripped (an encrypted backend persists it) — so it is not a
 * drop-in replacement here. */
static gboolean
imported_item_matches (const GPasteItem *source,
                       const GPasteItem *written)
{
    if (!g_paste_str_equal (g_paste_item_get_kind (source), g_paste_item_get_kind (written)))
        return FALSE;

    if (_G_PASTE_IS_IMAGE_ITEM (source))
        return g_paste_str_equal (g_paste_image_item_get_checksum (_G_PASTE_IMAGE_ITEM (source)),
                                  g_paste_image_item_get_checksum (_G_PASTE_IMAGE_ITEM (written)));

    return g_paste_str_equal (g_paste_item_get_real_value (source), g_paste_item_get_real_value (written));
}

/* Returns TRUE only if every history was copied into @chosen and reads back
 * with the same items — matched by uuid in order, contents verified — so the
 * caller never deletes the originals on a genuinely failed write (e.g. an
 * encrypted write that ran out of memory deriving the key, or a row that
 * committed with a corrupt payload). Two things are deliberately not failures:
 * the destination legitimately dropping items it cannot store (only the plain
 * flavors, which never persist passwords — an encrypted destination does, so a
 * password missing there *is* a failure), and in-memory sizes differing across
 * backends (an image read back from a database blob carries its PNG bytes
 * while a path-based one does not). */
static gboolean
import_histories (GPasteSettings *settings,
                  GPasteStorage   current,
                  const gchar    *current_passphrase,
                  GPasteStorage   chosen,
                  const gchar    *chosen_passphrase)
{
    /* Both keys are passed explicitly: source and destination may both be
     * encrypted under two different passphrases, which the single process-wide
     * one cannot express. */
    g_autoptr (GPasteStorageBackend) previous = g_paste_storage_backend_new_with_passphrase (current, settings, current_passphrase);
    g_autoptr (GPasteStorageBackend) next = g_paste_storage_backend_new_with_passphrase (chosen, settings, chosen_passphrase);
    g_auto (GStrv) names = g_paste_storage_backend_list_histories (previous, NULL);
    /* Only the plain flavors drop password items; an encrypted destination stores
     * them, so a password missing from the read-back is a genuine write failure
     * there and must not be waved through (the caller may then delete the source). */
    const gboolean chosen_stores_passwords = g_paste_storage_is_encrypted (chosen);
    gboolean ok = TRUE;

    if (!names)
        return FALSE;

    for (GStrv name = names; ok && *name; ++name)
    {
        GList *history = NULL;
        gsize size = 0;

        /* Never let a source we could not actually read (a wrong passphrase, a
         * transient I/O error) pass as an empty history: writing that "empty"
         * into the destination and reporting success would let the caller delete
         * the still-intact originals. Bail out instead so the migration is kept
         * for a retry with the data untouched. */
        if (!g_paste_storage_backend_read_history (previous, *name, &history, &size))
        {
            g_list_free_full (history, g_object_unref);
            ok = FALSE;
            break;
        }

        g_paste_storage_backend_write_history (next, *name, history);

        GList *written = NULL;
        gsize written_size = 0;

        g_paste_storage_backend_read_history (next, *name, &written, &written_size);

        const GList *w = written;

        for (const GList *h = history; ok && h; h = g_list_next (h))
        {
            if (w && g_paste_str_equal (g_paste_item_get_uuid (h->data), g_paste_item_get_uuid (w->data)))
            {
                ok = imported_item_matches (h->data, w->data);
                w = g_list_next (w);
            }
            else
                ok = !chosen_stores_passwords && g_paste_str_equal (g_paste_item_get_kind (h->data), "Password");
        }

        /* Nothing may read back that was not written. */
        if (w)
            ok = FALSE;

        g_list_free_full (written, g_object_unref);
        g_list_free_full (history, g_object_unref);
    }

    return ok;
}

static void
cleanup_histories (GPasteSettings *settings,
                   GPasteStorage   current,
                   const gchar    *current_passphrase)
{
    g_autoptr (GPasteStorageBackend) previous = g_paste_storage_backend_new_with_passphrase (current, settings, current_passphrase);
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
    /* The source keeps the key it was unlocked with, the destination gets the
     * one just set (the process-wide passphrase), so an encrypted -> encrypted
     * migration re-keys the history instead of reading the old data with the
     * new key. Both %NULL for the plain flavors, which need none. */
    const gchar *current_passphrase = NULL;
    const gchar *chosen_passphrase = NULL;

#ifdef G_PASTE_ENABLE_ENCRYPTION
    current_passphrase = self->source_passphrase;
    chosen_passphrase = g_paste_storage_backend_get_passphrase ();
#endif

    self->applied = TRUE;

    /* Import (if any) before switching the backend, so a failed write never leaves
     * the daemon pointed at an empty new backend with the real data orphaned. */
    gboolean imported = TRUE;

    if (import && can_import (self, chosen))
        imported = import_histories (self->settings, self->current, current_passphrase, chosen, chosen_passphrase);

    if (import && !imported)
    {
        /* The import genuinely failed: keep the current backend, data and revision
         * untouched so nothing is lost or hidden and the migration is offered
         * again next time rather than silently leaving an empty new backend. */
        g_warning ("History import failed; keeping the current storage backend and old data");

#ifdef G_PASTE_ENABLE_ENCRYPTION
        /* We are staying on the source backend, so the process has to go back to
         * the passphrase that opens it — which is the one we unlocked it with, or
         * none when it is a plain flavor. Leaving the destination's in place
         * would look to everything downstream like the history is already
         * unlocked: no prompt, and a backend built with a key its data does not
         * take. */
        g_paste_storage_backend_set_passphrase (self->source_passphrase);
#endif
    }
    else
    {
        g_paste_settings_set_storage_backend (self->settings, chosen);

        /* Only delete the old data once the import is confirmed, so a failed
         * import can never wipe the history it was meant to migrate. */
        if (cleanup && imported)
            cleanup_histories (self->settings, self->current, current_passphrase);

        g_paste_settings_set_storage_backend_revision (self->settings, G_PASTE_STORAGE_BACKEND_REVISION);

#if defined(G_PASTE_ENABLE_ENCRYPTION) && defined(G_PASTE_ENABLE_LIBSECRET)
        /* The new passphrase now opens what is actually on disk, so this is when
         * the user's choice about remembering it can be honoured. Only when one
         * was asked for *and* the destination is still the encrypted backend it
         * was asked for: anything else never offered the choice, and must not
         * silently discard an entry that still unlocks a history. */
        if (self->passphrase_prompted && g_paste_storage_is_encrypted (chosen))
        {
            if (self->remember_passphrase == G_PASTE_STORAGE_REMEMBER_YES)
                g_paste_storage_keyring_store (chosen_passphrase);
            else if (self->remember_passphrase == G_PASTE_STORAGE_REMEMBER_NO)
                g_paste_storage_keyring_clear ();
        }
        /* Leaving encryption behind for good: once the encrypted history it
         * opened is gone, a remembered passphrase is a secret kept for nothing.
         * Only once it is actually gone, though — data we kept is still locked
         * with it, and migrating back would want it. */
        else if (g_paste_storage_is_encrypted (self->current) && !g_paste_storage_is_encrypted (chosen) &&
                 cleanup && imported)
            g_paste_storage_keyring_clear ();
#endif
    }

    /* Destroying the window frees self (its data); grab the callback first. */
    GPasteStorageMigrationDoneFunc done = self->done;
    gpointer done_data = self->user_data;

    gtk_window_destroy (self->window);

    if (done)
        done (done_data);
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
typedef void (*UnlockDoneFunc) (gpointer user_data);

typedef struct
{
    GtkApplication *application;
    GPasteSettings *settings;
    GPasteStorage   storage_kind; /* the encrypted flavour being unlocked */
    UnlockDoneFunc  done;
    gpointer        user_data;
} UnlockPrompt;

static void
on_unlock_reply (const gchar          *passphrase,
                 GPasteStorageRemember remember,
                 gpointer               user_data)
{
    UnlockPrompt *prompt = user_data;

    /* A wrong passphrase would load an empty history and let the next save
     * overwrite the real data, so never accept one that does not decrypt: ask
     * again instead. */
    if (passphrase && !g_paste_storage_passphrase_can_decrypt (prompt->storage_kind, prompt->settings, passphrase))
    {
        /* Carry the switch's state into the retry: it is the same question, and
         * re-deriving it from the keyring would quietly undo the answer. */
        g_paste_storage_migration_prompt_passphrase (prompt->application, FALSE,
                                                     _("Wrong passphrase, please try again"),
                                                     remember, on_unlock_reply, prompt);
        return;
    }

    /* NULL on dismissal: leave no passphrase set. */
    if (passphrase)
    {
        g_paste_storage_backend_set_passphrase (passphrase);

#ifdef G_PASTE_ENABLE_LIBSECRET
        /* This is the point where the passphrase is proven, so it is the only
         * safe place to remember it: a wrong one is never written, and turning
         * the switch off drops whatever was remembered before. */
        if (remember == G_PASTE_STORAGE_REMEMBER_YES)
            g_paste_storage_keyring_store (passphrase);
        else if (remember == G_PASTE_STORAGE_REMEMBER_NO)
            g_paste_storage_keyring_clear ();
#else
        (void) remember;
#endif
    }

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
               GPasteStorage   storage_kind,
               UnlockDoneFunc  done,
               gpointer        user_data)
{
    UnlockPrompt *prompt = g_new0 (UnlockPrompt, 1);

    prompt->application = application;
    prompt->settings = settings;
    prompt->storage_kind = storage_kind;
    prompt->done = done;
    prompt->user_data = user_data;

    g_paste_storage_migration_prompt_passphrase (application, FALSE, NULL,
                                                 G_PASTE_STORAGE_REMEMBER_UNCHANGED, on_unlock_reply, prompt);
}

static void continue_apply (MigrationData *self);

/* Take a copy of the (already verified) passphrase unlocking the source. */
static void
set_source_passphrase (MigrationData *self,
                       const gchar   *passphrase)
{
    g_paste_storage_passphrase_free (self->source_passphrase);
    self->source_passphrase = g_paste_storage_passphrase_dup (passphrase);
}

static void
on_passphrase_set (const gchar          *passphrase,
                   GPasteStorageRemember remember,
                   gpointer               user_data)
{
    MigrationData *self = user_data;

    /* Cancelled: stay on the migration dialog so another backend can be picked. */
    if (!passphrase)
        return;

    /* Remembering it waits until the migration has applied — see
     * apply_migration(). */
    self->passphrase_prompted = TRUE;
    self->remember_passphrase = remember;

    /* The destination's key, and from now on the daemon's: the callers all read
     * it back from here once the migration is done (the gnome-shell helper hands
     * it to the shell, the standalone daemon skips its unlock prompt). */
    g_paste_storage_backend_set_passphrase (passphrase);
    apply_migration (self, backend_for_index (self, adw_combo_row_get_selected (self->backend_row)));
}

/* Once the source encrypted history is unlocked, keep its passphrase aside (the
 * process-wide one is about to be replaced by the destination's) and carry on
 * with the next step; on dismissal (no passphrase) stay on the dialog instead. */
static void
on_source_unlocked (gpointer user_data)
{
    MigrationData *self = user_data;
    const gchar *passphrase = g_paste_storage_backend_get_passphrase ();

    if (!passphrase)
        return;

    set_source_passphrase (self, passphrase);

    continue_apply (self);
}
#endif

/* Apply the chosen backend, gathering whatever passphrases are still missing
 * first: the source's (to read the encrypted history we import from or delete)
 * then the destination's (a new one, so a re-keying migration never silently
 * reuses the source's). Each prompt re-enters here once answered, and the state
 * gathered so far is kept, so a second Apply only asks what is still missing. */
static void
continue_apply (MigrationData *self)
{
    GPasteStorage chosen = backend_for_index (self, adw_combo_row_get_selected (self->backend_row));

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* Importing from (or deleting) an existing encrypted history needs its
     * passphrase to read or list it. Prefer one this process already holds, then
     * one remembered in the keyring, and only prompt when there is none or it has
     * gone stale. Done before asking for the destination's passphrase so a
     * wrong-passphrase retry happens before the user has picked a new one, not
     * after. */
    if (g_paste_storage_is_encrypted (self->current) && !self->source_passphrase &&
        (adw_switch_row_get_active (self->import_row) || adw_switch_row_get_active (self->cleanup_row)))
    {
        const gchar *known = g_paste_storage_backend_get_passphrase ();

        if (known && g_paste_storage_passphrase_can_decrypt (self->current, self->settings, known))
            set_source_passphrase (self, known);
#ifdef G_PASTE_ENABLE_LIBSECRET
        else if (g_paste_storage_keyring_apply_verified (self->current, self->settings))
            set_source_passphrase (self, g_paste_storage_backend_get_passphrase ());
#endif
        else
        {
            unlock_prompt (self->application, self->settings, self->current, on_source_unlocked, self);
            return;
        }
    }

    /* Switching to encrypted storage always needs a new passphrase to store with,
     * confirmed (and optionally remembered in the keyring) by the prompt. That
     * holds just as much when the source was encrypted too: the history is
     * re-keyed, never handed the source's passphrase behind the user's back.
     * Keeping the existing encrypted backend is the one case that needs nothing
     * here: its passphrase is obtained later through the daemon's normal unlock
     * flow. */
    if (g_paste_storage_is_encrypted (chosen) && chosen != self->current)
    {
        g_paste_storage_migration_prompt_passphrase (self->application, TRUE, NULL,
                                                     G_PASTE_STORAGE_REMEMBER_UNCHANGED, on_passphrase_set, self);
        return;
    }
#endif

    apply_migration (self, chosen);
}

static void
on_apply (GtkButton *button G_GNUC_UNUSED,
          gpointer   user_data)
{
    continue_apply (user_data);
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
#ifdef G_PASTE_ENABLE_SQLITE
    gtk_string_list_append (backends, _("Store the history in a database"));
    self->backends[self->n_backends++] = G_PASTE_STORAGE_SQLITE;
#ifdef G_PASTE_ENABLE_ENCRYPTION
    gtk_string_list_append (backends, _("Store the history in an encrypted database"));
    self->backends[self->n_backends++] = G_PASTE_STORAGE_ENCRYPTED_SQLITE;
#endif
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
    /* Where the switch started, so "left alone" can be told from "turned off"
     * — only the latter may delete what is in the keyring. */
    gboolean                    remembered;
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
passphrase_deliver (PassphraseData        *self,
                    const gchar           *passphrase,
                    GPasteStorageRemember  remember)
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
        self->done (passphrase, remember, self->user_data);
}

static void
on_passphrase_ok (GtkButton *button G_GNUC_UNUSED,
                  gpointer   user_data)
{
    PassphraseData *self = user_data;
    GPasteStorageRemember remember = G_PASTE_STORAGE_REMEMBER_UNCHANGED;

#ifdef G_PASTE_ENABLE_LIBSECRET
    /* Only report the choice: the keyring is written by whoever established that
     * this passphrase is the right one, which cannot be known from here. Turning
     * the switch off is a request to forget; finding it off and leaving it there
     * is not, so a keyring we could not read is never taken as permission to
     * delete what it holds. */
    if (self->remember && adw_switch_row_get_active (ADW_SWITCH_ROW (self->remember)))
        remember = G_PASTE_STORAGE_REMEMBER_YES;
    else if (self->remembered)
        remember = G_PASTE_STORAGE_REMEMBER_NO;
#endif

    /* Deliver while the entry text is still alive; the callback copies it into
     * secure memory. gtk_window_destroy() does not emit "close-request". */
    passphrase_deliver (self, passphrase_text (self->entry), remember);
    gtk_window_destroy (self->window);
}

static gboolean
on_passphrase_close (GtkWindow *window G_GNUC_UNUSED,
                     gpointer   user_data)
{
    passphrase_deliver (user_data, NULL, G_PASTE_STORAGE_REMEMBER_UNCHANGED);

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
                                             GPasteStorageRemember        remember_initially,
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
#else
        /* Built without libpwquality, so there is no rating to give. Say so
         * rather than leave the row out: someone choosing a passphrase should
         * know it is not being judged, instead of reading a silent absence as
         * approval. */
        GtkWidget *strength_row = adw_action_row_new ();

        adw_preferences_row_set_title (ADW_PREFERENCES_ROW (strength_row), _("Passphrase strength"));
        adw_action_row_set_subtitle (ADW_ACTION_ROW (strength_row), _("Not available in this build"));
        gtk_widget_set_sensitive (strength_row, FALSE);
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
    /* Start from what the user already chose: someone who had their passphrase
     * remembered stays opted in, so changing it replaces the stored one instead
     * of leaving a stale entry behind. It also repairs one — this prompt only
     * comes up when the keyring did not unlock the history, so an entry that
     * exists here is a bad one, and keeping the switch on overwrites it. A
     * re-prompt carries the choice made on the previous attempt instead. */
    self->remembered = (remember_initially == G_PASTE_STORAGE_REMEMBER_UNCHANGED)
                     ? g_paste_storage_keyring_has_passphrase ()
                     : (remember_initially == G_PASTE_STORAGE_REMEMBER_YES);
    adw_switch_row_set_active (ADW_SWITCH_ROW (remember), self->remembered);
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), remember);
#else
    /* No keyring to remember anything in, so there is no switch to place. */
    (void) remember_initially;
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
    if (!g_paste_storage_is_encrypted (g_paste_settings_get_storage_backend (settings)) ||
        g_paste_storage_backend_get_passphrase ())
        return FALSE;

#ifdef G_PASTE_ENABLE_LIBSECRET
    /* A keyring passphrase that unlocks the history is applied here, so no prompt
     * (and, in gnome-shell, no helper) is needed: the caller can load straight
     * away with the passphrase now set in this process. */
    if (g_paste_storage_keyring_apply_verified (g_paste_settings_get_storage_backend (settings), settings))
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
    unlock_prompt (application, settings, g_paste_settings_get_storage_backend (settings), done, user_data);
#else
    if (done)
        done (user_data);
#endif
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
typedef struct
{
    GtkApplication                *application;
    GPasteSettings                *settings;
    GPasteStorageMigrationDoneFunc done;
    gpointer                       user_data;

    /* The encrypted flavor being re-keyed, and the passphrase it is currently
     * encrypted with once we have one that actually decrypts it (in gcr secure
     * memory). */
    GPasteStorage                  storage_kind;
    gchar                         *current_passphrase;
} RekeyData;

static void
rekey_finish (RekeyData *self)
{
    GPasteStorageMigrationDoneFunc done = self->done;
    gpointer done_data = self->user_data;

    g_paste_storage_passphrase_free (self->current_passphrase);
    g_object_unref (self->settings);
    g_free (self);

    if (done)
        done (done_data);
}

/* Re-encrypt every history of the current flavor with @passphrase. Each one is
 * re-keyed atomically, but they go one at a time: stop at the first failure
 * rather than carrying on, so what is left is "these are on the new passphrase,
 * those are still on the old" — recoverable by running again — instead of a set
 * silently split further apart. */
/* Re-key @name on its own backend: a backend caches whatever it last opened, so
 * giving each history a fresh one keeps a re-keyed database from being reopened
 * through an instance that still holds the old key. */
static gboolean
rekey_history (RekeyData   *self,
               const gchar *name,
               const gchar *from,
               const gchar *to)
{
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (self->storage_kind,
                                                                                            self->settings,
                                                                                            from);

    return g_paste_storage_backend_rekey (backend, name, to);
}

static gboolean
rekey_histories (RekeyData   *self,
                 const gchar *passphrase)
{
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (self->storage_kind,
                                                                                            self->settings,
                                                                                            self->current_passphrase);
    g_auto (GStrv) names = g_paste_storage_backend_list_histories (backend, NULL);

    /* Not "no histories" (that is an empty list): the listing itself failed, so
     * we have no idea what would be left behind. */
    if (!names)
    {
        g_warning ("Could not list the histories to change their passphrase");
        return FALSE;
    }

    for (GStrv name = names; *name; ++name)
    {
        if (rekey_history (self, *name, self->current_passphrase, passphrase))
            continue;

        g_warning ("Failed to change the passphrase of the history \"%s\"", *name);

        /* Put the ones that did move back on the passphrase they all still
         * shared a moment ago, rather than leave the set split between two — a
         * split one cannot be repaired by running this again, since it starts
         * from a single passphrase. Each history's re-key either happened whole
         * or not at all, so the failing one needs nothing undone. */
        for (GStrv done = names; done != name; ++done)
        {
            if (!rekey_history (self, *done, passphrase, self->current_passphrase))
                g_warning ("The history \"%s\" is left on the new passphrase while the others keep the old one", *done);
        }

        return FALSE;
    }

    /* An encrypted backend with nothing stored under it is not something we can
     * meaningfully re-key, and it is also what a flavor this build cannot
     * construct degrades to — adopting a passphrase then, and remembering it,
     * would hand out a key that opens nothing. */
    if (!*names)
    {
        g_warning ("There is no stored history to change the passphrase of");
        return FALSE;
    }

    return TRUE;
}

static void
on_rekey_passphrase_set (const gchar          *passphrase,
                         GPasteStorageRemember remember,
                         gpointer               user_data)
{
    RekeyData *self = user_data;

    /* Dismissed: nothing has been touched yet, so there is nothing to undo. */
    if (!passphrase)
    {
        rekey_finish (self);
        return;
    }

    /* Only adopt the new passphrase once the data on disk actually speaks it,
     * or the daemon would reload an unreadable history — and only remember it
     * then, for the same reason. A re-key that gave up part way leaves the
     * keyring alone: it still holds the passphrase the untouched histories
     * take. */
    if (rekey_histories (self, passphrase))
    {
        g_paste_storage_backend_set_passphrase (passphrase);

#ifdef G_PASTE_ENABLE_LIBSECRET
        if (remember == G_PASTE_STORAGE_REMEMBER_YES)
            g_paste_storage_keyring_store (passphrase);
        else if (remember == G_PASTE_STORAGE_REMEMBER_NO)
            g_paste_storage_keyring_clear ();
#else
        (void) remember;
#endif
    }

    rekey_finish (self);
}

/* The current history is unlocked: ask for the passphrase to replace it with.
 * Same prompt the migration uses to set up a new encrypted history — two
 * entries, the strength meter, the data-loss warning, and the switch that
 * remembers it in the keyring (which is what updates the stored one). */
static void
rekey_prompt_new_passphrase (RekeyData *self)
{
    g_paste_storage_migration_prompt_passphrase (self->application, TRUE, NULL,
                                                 G_PASTE_STORAGE_REMEMBER_UNCHANGED, on_rekey_passphrase_set, self);
}

static void
on_rekey_source_unlocked (gpointer user_data)
{
    RekeyData *self = user_data;
    const gchar *passphrase = g_paste_storage_backend_get_passphrase ();

    /* Dismissed without unlocking: there is no passphrase to change. */
    if (!passphrase)
    {
        rekey_finish (self);
        return;
    }

    self->current_passphrase = g_paste_storage_passphrase_dup (passphrase);

    rekey_prompt_new_passphrase (self);
}
#endif

/**
 * g_paste_storage_rekey_show:
 * @application: the #GtkApplication to anchor the dialogs to
 * @settings: a #GPasteSettings instance
 * @done: (scope async) (nullable): called once the passphrase was changed, or the prompt dismissed
 * @user_data: data passed to @done
 *
 * Change the passphrase of the encrypted history, re-encrypting everything with
 * the new one. Unlike the migration dialog this never moves the history: the
 * data stays in the backend it is in, only its key changes.
 *
 * Unlocks the history first (from this process, the keyring, or a prompt), then
 * asks for the new passphrase with a confirmation. @done is invoked immediately
 * when the history is not encrypted, so there is no passphrase to change.
 * Like the other dialogs here, the caller owns the main loop.
 */
G_PASTE_VISIBLE void
g_paste_storage_rekey_show (GtkApplication                 *application,
                            GPasteSettings                 *settings,
                            GPasteStorageMigrationDoneFunc  done,
                            gpointer                        user_data)
{
    g_return_if_fail (GTK_IS_APPLICATION (application));
    g_return_if_fail (_G_PASTE_IS_SETTINGS (settings));

#ifdef G_PASTE_ENABLE_ENCRYPTION
    GPasteStorage storage_kind = g_paste_settings_get_storage_backend (settings);

    if (g_paste_storage_is_encrypted (storage_kind))
    {
        RekeyData *self = g_new0 (RekeyData, 1);

        self->application = application;
        self->settings = g_object_ref (settings);
        self->done = done;
        self->user_data = user_data;
        self->storage_kind = storage_kind;

        /* The passphrase this process already holds is the common case (the
         * daemon has been serving the history with it), then the keyring, then
         * ask — the same order the migration unlocks a source with. */
        const gchar *known = g_paste_storage_backend_get_passphrase ();

        if (known && g_paste_storage_passphrase_can_decrypt (storage_kind, settings, known))
            self->current_passphrase = g_paste_storage_passphrase_dup (known);
#ifdef G_PASTE_ENABLE_LIBSECRET
        else if (g_paste_storage_keyring_apply_verified (storage_kind, settings))
            self->current_passphrase = g_paste_storage_passphrase_dup (g_paste_storage_backend_get_passphrase ());
#endif
        else
        {
            unlock_prompt (application, settings, storage_kind, on_rekey_source_unlocked, self);
            return;
        }

        rekey_prompt_new_passphrase (self);

        return;
    }

    g_warning ("The history is not encrypted: there is no passphrase to change");
#endif

    if (done)
        done (user_data);
}
