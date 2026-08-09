// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-util.h>

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
    GPastePrompt                  *prompt;
    GPasteStorageMigrationDoneFunc done;
    gpointer                       user_data;

    /* The backend the history currently lives in (detected from the files on
     * disk): what we import from and clean up. */
    GPasteStorage                  current;

    /* What the migration prompt last came back with. */
    GPasteStorage                  chosen;
    gboolean                       import;
    gboolean                       cleanup;

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* The passphrase unlocking @current, once the keyring or the unlock prompt
     * has produced one that actually decrypts it. Kept here rather than in the
     * process-wide global because migrating between two encrypted flavors needs
     * the source key while the destination is being (re)keyed with another one.
     * Doubles as the "source already unlocked" flag, so a second Apply does not
     * ask again. In gcr secure memory; freed in migration_data_free(). */
    GPastePassphrase              *source_passphrase;

    /* Whether a new passphrase was asked for (only then is there a choice to
     * honour), and whether the user asked to remember it. Acted on once the
     * migration has actually applied, so a failed one never leaves the keyring
     * pointing at a history that was never written. */
    gboolean                       passphrase_prompted;
    GPasteStorageRemember          remember_passphrase;
#endif

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

/* Release the state and hand control back to the caller: the one way out of the
 * migration, whether it applied, was dismissed or never got started. */
static void
migration_finish (MigrationData *self)
{
    GPasteStorageMigrationDoneFunc done = self->done;
    gpointer done_data = self->user_data;

#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_paste_passphrase_free (self->source_passphrase);
#endif

    g_object_unref (self->settings);
    g_object_unref (self->prompt);
    g_free (self);

    if (done)
        done (done_data);
}

G_PASTE_VISIBLE gboolean
g_paste_storage_migration_needed (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), FALSE);

    return g_paste_settings_get_storage_backend_revision (settings) != G_PASTE_STORAGE_BACKEND_REVISION;
}

/* Whether @written faithfully reproduces @source: same kind, and same content
 * — an image's identity is its checksum (its value is a per-backend cache
 * path), everything else compares by real value. This deliberately differs from
 * g_paste_item_equals(): that dedup predicate ignores the kind and treats two
 * distinct passwords as never equal, whereas migration must confirm a password's
 * real value round-tripped (an encrypted backend persists it) — so it is not a
 * drop-in replacement here. */
static gboolean
imported_item_matches (GPasteItem *source,
                       GPasteItem *written)
{
    if (!g_paste_str_equal (g_paste_item_get_kind (source), g_paste_item_get_kind (written)))
        return FALSE;

    if (G_PASTE_IS_IMAGE_ITEM (source))
        return g_paste_str_equal (g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (source)),
                                  g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (written)));

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

/* What the import needs, copied so the worker touches nothing the main thread
 * still owns. The passphrases are its own references into secure memory. */
typedef struct
{
    GPasteSettings   *settings;
    GPasteStorage     current;
    GPasteStorage     chosen;
    GPastePassphrase *current_passphrase;
    GPastePassphrase *chosen_passphrase;
} ImportWork;

static void
import_work_free (gpointer data)
{
    ImportWork *work = data;

    g_paste_passphrase_free (work->current_passphrase);
    g_paste_passphrase_free (work->chosen_passphrase);
    g_object_unref (work->settings);
    g_free (work);
}

/* Runs on a worker thread. Importing an encrypted history derives an Argon2id
 * key per stream — read the source, write the destination, read it back to
 * verify — at MODERATE limits, so a few hundred milliseconds and a few hundred
 * megabytes each, several times over per history. On the main loop that is a
 * frozen daemon; hosted inside gnome-shell it is a frozen desktop, which is
 * what this exists to avoid. Nothing else touches the store meanwhile: a
 * migration runs before the daemon is built, or after it has been flushed and
 * stopped. */
static void
import_work_run (GTask        *task,
                 gpointer      source      G_GNUC_UNUSED,
                 gpointer      task_data,
                 GCancellable *cancellable G_GNUC_UNUSED)
{
    ImportWork *work = task_data;

    g_task_return_boolean (task, import_histories (work->settings,
                                                   work->current,
                                                   g_paste_passphrase_peek (work->current_passphrase),
                                                   work->chosen,
                                                   g_paste_passphrase_peek (work->chosen_passphrase)));
}

/* Everything after the import: settings, cleanup and keyring, all back on the
 * main thread where their ordering against the rest of the daemon holds. */
static void
apply_migration_settle (MigrationData *self,
                        gboolean       imported)
{
    GPasteStorage chosen = self->chosen;
    gboolean import = self->import;
    gboolean cleanup = self->cleanup;
    /* The source keeps the key it was unlocked with, the destination gets the
     * one just set (the process-wide passphrase), so an encrypted -> encrypted
     * migration re-keys the history instead of reading the old data with the
     * new key. Both %NULL for the plain flavors, which need none. */
    const gchar *current_passphrase = NULL;
    const gchar *chosen_passphrase = NULL;

#ifdef G_PASTE_ENABLE_ENCRYPTION
    current_passphrase = g_paste_passphrase_peek (self->source_passphrase);
    chosen_passphrase = g_paste_storage_backend_get_passphrase ();
#endif

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
        g_paste_storage_backend_set_passphrase (g_paste_passphrase_peek (self->source_passphrase));
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

    migration_finish (self);
}

static void
on_import_done (GObject      *source G_GNUC_UNUSED,
                GAsyncResult *result,
                gpointer      user_data)
{
    apply_migration_settle (user_data, g_task_propagate_boolean (G_TASK (result), NULL));
}

static void
apply_migration (MigrationData *self)
{
    /* Import (if any) before switching the backend, so a failed write never
     * leaves the daemon pointed at an empty new backend with the real data
     * orphaned. Nothing else below is expensive — deleting the old data unlinks
     * files and derives no key — so this is the only step that goes to a thread. */
    if (!self->import)
    {
        apply_migration_settle (self, TRUE);
        return;
    }

    ImportWork *work = g_new0 (ImportWork, 1);

    work->settings = g_object_ref (self->settings);
    work->current = self->current;
    work->chosen = self->chosen;
#ifdef G_PASTE_ENABLE_ENCRYPTION
    work->current_passphrase = g_paste_passphrase_copy (self->source_passphrase);
    work->chosen_passphrase = g_paste_passphrase_new (g_paste_storage_backend_get_passphrase ());
#endif

    g_autoptr (GTask) task = g_task_new (NULL, NULL, on_import_done, self);

    g_task_set_source_tag (task, apply_migration);
    g_task_set_task_data (task, work, import_work_free);
    g_task_run_in_thread (task, import_work_run);
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* @unlocked says whether a passphrase that actually decrypts is now installed.
 * Asking the process-wide global instead would be wrong: continue_apply() only
 * prompts when the global is absent *or* does not decrypt this flavour, so on a
 * dismissal a stale one is still sitting there and would read as success. */
typedef void (*UnlockDoneFunc) (gboolean unlocked,
                                gpointer user_data);

typedef struct
{
    GPastePrompt   *prompt;
    GPasteSettings *settings;
    GPasteStorage   storage_kind; /* the encrypted flavour being unlocked */
    UnlockDoneFunc  done;
    gpointer        user_data;
} UnlockPrompt;

static void
on_unlock_reply (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
    UnlockPrompt *prompt = user_data;
    GPasteStorageRemember remember;
    g_autoptr (GPastePassphrase) passphrase = g_paste_prompt_passphrase_finish (G_PASTE_PROMPT (source), result,
                                                                                &remember, NULL);

    /* A wrong passphrase would load an empty history and let the next save
     * overwrite the real data, so never accept one that does not decrypt: ask
     * again instead. */
    const gchar *cleartext = g_paste_passphrase_peek (passphrase);

    if (passphrase && !g_paste_storage_passphrase_can_decrypt (prompt->storage_kind, prompt->settings, cleartext))
    {
        /* Carry the switch's state into the retry: it is the same question, and
         * re-deriving it from the keyring would quietly undo the answer. */
        g_paste_prompt_passphrase_async (prompt->prompt, FALSE,
                                         _("Wrong passphrase, please try again"),
                                         remember, on_unlock_reply, prompt);
        return;
    }

    /* NULL on dismissal: leave no passphrase set. */
    if (passphrase)
    {
        g_paste_storage_backend_set_passphrase (cleartext);

#ifdef G_PASTE_ENABLE_LIBSECRET
        /* This is the point where the passphrase is proven, so it is the only
         * safe place to remember it: a wrong one is never written, and turning
         * the switch off drops whatever was remembered before. */
        if (remember == G_PASTE_STORAGE_REMEMBER_YES)
            g_paste_storage_keyring_store (cleartext);
        else if (remember == G_PASTE_STORAGE_REMEMBER_NO)
            g_paste_storage_keyring_clear ();
#else
        (void) remember;
#endif
    }

    UnlockDoneFunc done = prompt->done;
    gpointer done_data = prompt->user_data;
    gboolean unlocked = (passphrase != NULL);

    g_object_unref (prompt->prompt);
    g_object_unref (prompt->settings);
    g_free (prompt);

    if (done)
        done (unlocked, done_data);
}

/* Shared "unlock an existing encrypted history" prompt: ask for the passphrase,
 * verify it actually decrypts, re-prompt on a wrong one, then call @done once
 * settled (the passphrase is set on success, left unset on dismissal). Callers
 * try the keyring first; this is the prompt half they fall back to. */
static void
unlock_prompt (GPastePrompt   *ui,
               GPasteSettings *settings,
               GPasteStorage   storage_kind,
               UnlockDoneFunc  done,
               gpointer        user_data)
{
    UnlockPrompt *prompt = g_new0 (UnlockPrompt, 1);

    /* This outlives the call — for as long as the dialog is up, and across
     * every wrong-passphrase retry — so it cannot borrow either of them. The
     * shell host drops its last reference to the settings the moment the
     * extension is disabled, which can happen with the prompt still on screen. */
    prompt->prompt = g_object_ref (ui);
    prompt->settings = g_object_ref (settings);
    prompt->storage_kind = storage_kind;
    prompt->done = done;
    prompt->user_data = user_data;

    g_paste_prompt_passphrase_async (ui, FALSE, NULL, G_PASTE_STORAGE_REMEMBER_UNCHANGED,
                                     on_unlock_reply, prompt);
}

static void continue_apply (MigrationData *self);
static void ask_migration (MigrationData *self);

/* Take a copy of the (already verified) passphrase unlocking the source. */
static void
set_source_passphrase (MigrationData *self,
                       const gchar   *passphrase)
{
    g_paste_passphrase_free (self->source_passphrase);
    self->source_passphrase = g_paste_passphrase_new (passphrase);
}

static void
on_passphrase_set (GObject      *source,
                   GAsyncResult *result,
                   gpointer      user_data)
{
    MigrationData *self = user_data;
    GPasteStorageRemember remember;
    g_autoptr (GPastePassphrase) passphrase = g_paste_prompt_passphrase_finish (G_PASTE_PROMPT (source), result,
                                                                                &remember, NULL);

    /* Cancelled: ask again, so another backend can be picked. The state gathered
     * so far is kept, so the second time round only asks what is still
     * missing. */
    if (!passphrase)
    {
        ask_migration (self);
        return;
    }

    /* Remembering it waits until the migration has applied — see
     * apply_migration(). */
    self->passphrase_prompted = TRUE;
    self->remember_passphrase = remember;

    /* The destination's key, and from now on the daemon's: the callers all read
     * it back from here once the migration is done. */
    g_paste_storage_backend_set_passphrase (g_paste_passphrase_peek (passphrase));
    apply_migration (self);
}

/* Once the source encrypted history is unlocked, keep its passphrase aside (the
 * process-wide one is about to be replaced by the destination's) and carry on
 * with the next step; on dismissal (no passphrase) ask again instead. */
static void
on_source_unlocked (gboolean unlocked,
                    gpointer user_data)
{
    MigrationData *self = user_data;

    if (!unlocked)
    {
        ask_migration (self);
        return;
    }

    set_source_passphrase (self, g_paste_storage_backend_get_passphrase ());

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
#ifdef G_PASTE_ENABLE_ENCRYPTION
    GPasteStorage chosen = self->chosen;

    /* Importing from (or deleting) an existing encrypted history needs its
     * passphrase to read or list it. Prefer one this process already holds, then
     * one remembered in the keyring, and only prompt when there is none or it has
     * gone stale. Done before asking for the destination's passphrase so a
     * wrong-passphrase retry happens before the user has picked a new one, not
     * after. */
    if (g_paste_storage_is_encrypted (self->current) && !self->source_passphrase &&
        (self->import || self->cleanup))
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
            unlock_prompt (self->prompt, self->settings, self->current, on_source_unlocked, self);
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
        g_paste_prompt_passphrase_async (self->prompt, TRUE, NULL, G_PASTE_STORAGE_REMEMBER_UNCHANGED,
                                         on_passphrase_set, self);
        return;
    }
#endif

    apply_migration (self);
}

/* The migration prompt was answered. Dismissing it leaves the revision
 * untouched so it comes back on the next start: the user has to make a
 * deliberate choice, and the detected current backend (written in _show below)
 * is what this session keeps in the meantime. */
static void
on_migration_reply (GObject      *source,
                    GAsyncResult *result,
                    gpointer      user_data)
{
    MigrationData *self = user_data;

    if (!g_paste_prompt_migration_finish (G_PASTE_PROMPT (source), result,
                                          &self->chosen, &self->import, &self->cleanup, NULL))
    {
        migration_finish (self);
        return;
    }

    /* Both toggles are clamped into the fields rather than trusted: a prompt
     * backend is a public, introspectable interface now, and "delete the old
     * data" answered for a migration that is not moving anywhere would wipe the
     * current history with nothing imported to replace it. Our own backends grey
     * the toggles out, but that is their courtesy, not our guarantee. Clamping
     * the fields rather than a pair of locals is what keeps the two spellings
     * from meaning different things further down — and clamping them here, as
     * the answer comes in, is what keeps continue_apply() from making the user
     * unlock an encrypted history for an import and a cleanup that will not
     * happen. */
    self->import = self->import && g_paste_prompt_can_import (self->current, self->chosen);
    self->cleanup = self->cleanup && g_paste_prompt_backend_changes (self->current, self->chosen);

    continue_apply (self);
}

static void
ask_migration (MigrationData *self)
{
    guint n_offered;
    const GPasteStorage *offered = g_paste_prompt_list_storage_backends (&n_offered);

    g_paste_prompt_migration_async (self->prompt, offered, n_offered, self->current,
                                    on_migration_reply, self);
}

/**
 * g_paste_storage_migration_show:
 * @prompt: the #GPastePrompt to ask through
 * @settings: a #GPasteSettings instance
 * @done: (scope async) (nullable): called once the migration settled or was dismissed
 * @user_data: data passed to @done
 *
 * Ask the user where GPaste should store the history, and act on the answer.
 */
G_PASTE_VISIBLE void
g_paste_storage_migration_show (GPastePrompt                   *prompt,
                                GPasteSettings                 *settings,
                                GPasteStorageMigrationDoneFunc  done,
                                gpointer                        user_data)
{
    g_return_if_fail (G_PASTE_IS_PROMPT (prompt));
    g_return_if_fail (G_PASTE_IS_SETTINGS (settings));

    /* Detect the backend the history currently lives in from the files on disk,
     * and apply it right away so this session keeps the right backend even if the
     * prompt is dismissed without an explicit choice (importantly, an encrypted
     * history is not silently downgraded to "none"). */
    GPasteStorage current = detect_current_backend (settings);

    g_paste_settings_set_storage_backend (settings, current);

    MigrationData *self = g_new0 (MigrationData, 1);

    self->settings = g_object_ref (settings);
    self->prompt = g_object_ref (prompt);
    self->done = done;
    self->user_data = user_data;
    self->current = current;

    ask_migration (self);
}

G_PASTE_VISIBLE gboolean
g_paste_storage_decryption_needed (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), FALSE);

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* Only an encrypted history that is not already unlocked needs decrypting. */
    if (!g_paste_storage_is_encrypted (g_paste_settings_get_storage_backend (settings)) ||
        g_paste_storage_backend_get_passphrase ())
        return FALSE;

#ifdef G_PASTE_ENABLE_LIBSECRET
    /* A keyring passphrase that unlocks the history is applied here, so no prompt
     * is needed at all: the caller can load straight away with the passphrase now
     * set in this process. */
    if (g_paste_storage_keyring_apply_verified (g_paste_settings_get_storage_backend (settings), settings))
        return FALSE;
#endif

    return TRUE;
#else
    return FALSE;
#endif
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
typedef struct
{
    GPasteStorageMigrationDoneFunc done;
    gpointer                       user_data;
} DecryptionDone;

/* The caller carries on either way: an encrypted history that stayed locked
 * loads as unreadable, which the history already refuses to overwrite. */
static void
on_decryption_settled (gboolean unlocked G_GNUC_UNUSED,
                       gpointer user_data)
{
    g_autofree DecryptionDone *self = user_data;

    if (self->done)
        self->done (self->user_data);
}
#endif

/**
 * g_paste_storage_decryption_show:
 * @prompt: the #GPastePrompt to ask through
 * @settings: a #GPasteSettings instance
 * @done: (scope async) (nullable): called once the history is unlocked or the prompt dismissed
 * @user_data: data passed to @done
 *
 * Unlock an already-encrypted history through a passphrase prompt.
 */
G_PASTE_VISIBLE void
g_paste_storage_decryption_show (GPastePrompt                   *prompt,
                                 GPasteSettings                 *settings,
                                 GPasteStorageMigrationDoneFunc  done,
                                 gpointer                        user_data)
{
    g_return_if_fail (G_PASTE_IS_PROMPT (prompt));
    g_return_if_fail (G_PASTE_IS_SETTINGS (settings));

#ifdef G_PASTE_ENABLE_ENCRYPTION
    DecryptionDone *decryption = g_new0 (DecryptionDone, 1);

    decryption->done = done;
    decryption->user_data = user_data;

    unlock_prompt (prompt, settings, g_paste_settings_get_storage_backend (settings),
                   on_decryption_settled, decryption);
#else
    if (done)
        done (user_data);
#endif
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
typedef struct
{
    GPastePrompt                  *prompt;
    GPasteSettings                *settings;
    GPasteStorageMigrationDoneFunc done;
    gpointer                       user_data;

    /* The encrypted flavor being re-keyed, and the passphrase it is currently
     * encrypted with once we have one that actually decrypts it (in gcr secure
     * memory). */
    GPasteStorage                  storage_kind;
    GPastePassphrase              *current_passphrase;

    /* What the worker is re-keying to, and what to do about remembering it
     * once it has — both settled before the hop, acted on after. */
    GPastePassphrase              *new_passphrase;
    GPasteStorageRemember          remember;
} RekeyData;

static void
rekey_finish (RekeyData *self)
{
    GPasteStorageMigrationDoneFunc done = self->done;
    gpointer done_data = self->user_data;

    g_paste_passphrase_free (self->current_passphrase);
    g_paste_passphrase_free (self->new_passphrase);
    g_object_unref (self->settings);
    g_object_unref (self->prompt);
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
    const gchar *current = g_paste_passphrase_peek (self->current_passphrase);
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (self->storage_kind,
                                                                                            self->settings,
                                                                                            current);
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
        if (rekey_history (self, *name, current, passphrase))
            continue;

        g_warning ("Failed to change the passphrase of the history \"%s\"", *name);

        /* Put the ones that did move back on the passphrase they all still
         * shared a moment ago, rather than leave the set split between two — a
         * split one cannot be repaired by running this again, since it starts
         * from a single passphrase. Each history's re-key either happened whole
         * or not at all, so the failing one needs nothing undone. */
        for (GStrv done = names; done != name; ++done)
        {
            if (!rekey_history (self, *done, passphrase, current))
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
rekey_work_run (GTask        *task,
                gpointer      source      G_GNUC_UNUSED,
                gpointer      task_data   G_GNUC_UNUSED,
                GCancellable *cancellable G_GNUC_UNUSED)
{
    RekeyData *self = g_task_get_task_data (task);

    g_task_return_boolean (task, rekey_histories (self, g_paste_passphrase_peek (self->new_passphrase)));
}

/* Back on the main thread: only adopt the new passphrase once the data on disk
 * actually speaks it, or the daemon would reload an unreadable history — and
 * only remember it then, for the same reason. A re-key that gave up part way
 * leaves the keyring alone: it still holds the passphrase the untouched
 * histories take. */
static void
on_rekey_done (GObject      *source G_GNUC_UNUSED,
               GAsyncResult *result,
               gpointer      user_data)
{
    RekeyData *self = user_data;

    if (g_task_propagate_boolean (G_TASK (result), NULL))
    {
        const gchar *cleartext = g_paste_passphrase_peek (self->new_passphrase);

        g_paste_storage_backend_set_passphrase (cleartext);

#ifdef G_PASTE_ENABLE_LIBSECRET
        if (self->remember == G_PASTE_STORAGE_REMEMBER_YES)
            g_paste_storage_keyring_store (cleartext);
        else if (self->remember == G_PASTE_STORAGE_REMEMBER_NO)
            g_paste_storage_keyring_clear ();
#endif
    }

    rekey_finish (self);
}

static void
on_rekey_passphrase_set (GObject      *source,
                         GAsyncResult *result,
                         gpointer      user_data)
{
    RekeyData *self = user_data;
    GPasteStorageRemember remember;
    g_autoptr (GPastePassphrase) passphrase = g_paste_prompt_passphrase_finish (G_PASTE_PROMPT (source), result,
                                                                                &remember, NULL);

    /* Dismissed: nothing has been touched yet, so there is nothing to undo. */
    if (!passphrase)
    {
        rekey_finish (self);
        return;
    }

    /* Re-encrypting every history derives an Argon2id key per history, twice
     * over when one fails and the rest have to be put back — the same reason
     * the import runs off the main loop. Keep the passphrase and the choice
     * alive across the hop; the rest is decided back here. */
    self->new_passphrase = g_paste_passphrase_copy (passphrase);
    self->remember = remember;

    g_autoptr (GTask) task = g_task_new (NULL, NULL, on_rekey_done, self);

    g_task_set_source_tag (task, on_rekey_passphrase_set);
    /* Borrowed: rekey_finish() owns it, and it outlives the task either way. */
    g_task_set_task_data (task, self, NULL);
    g_task_run_in_thread (task, rekey_work_run);
}

/* The current history is unlocked: ask for the passphrase to replace it with.
 * Same prompt the migration uses to set up a new encrypted history — two
 * entries, the strength meter, the data-loss warning, and the switch that
 * remembers it in the keyring (which is what updates the stored one). */
static void
rekey_prompt_new_passphrase (RekeyData *self)
{
    g_paste_prompt_passphrase_async (self->prompt, TRUE, NULL, G_PASTE_STORAGE_REMEMBER_UNCHANGED,
                                     on_rekey_passphrase_set, self);
}

static void
on_rekey_source_unlocked (gboolean unlocked,
                          gpointer user_data)
{
    RekeyData *self = user_data;

    /* Dismissed without unlocking: there is no passphrase to change. */
    if (!unlocked)
    {
        rekey_finish (self);
        return;
    }

    self->current_passphrase = g_paste_passphrase_new (g_paste_storage_backend_get_passphrase ());

    rekey_prompt_new_passphrase (self);
}
#endif

/**
 * g_paste_storage_rekey_show:
 * @prompt: the #GPastePrompt to ask through
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
g_paste_storage_rekey_show (GPastePrompt                   *prompt,
                            GPasteSettings                 *settings,
                            GPasteStorageMigrationDoneFunc  done,
                            gpointer                        user_data)
{
    g_return_if_fail (G_PASTE_IS_PROMPT (prompt));
    g_return_if_fail (G_PASTE_IS_SETTINGS (settings));

#ifdef G_PASTE_ENABLE_ENCRYPTION
    GPasteStorage storage_kind = g_paste_settings_get_storage_backend (settings);

    if (g_paste_storage_is_encrypted (storage_kind))
    {
        RekeyData *self = g_new0 (RekeyData, 1);

        self->prompt = g_object_ref (prompt);
        self->settings = g_object_ref (settings);
        self->done = done;
        self->user_data = user_data;
        self->storage_kind = storage_kind;

        /* The passphrase this process already holds is the common case (the
         * daemon has been serving the history with it), then the keyring, then
         * ask — the same order the migration unlocks a source with. */
        const gchar *known = g_paste_storage_backend_get_passphrase ();

        if (known && g_paste_storage_passphrase_can_decrypt (storage_kind, settings, known))
            self->current_passphrase = g_paste_passphrase_new (known);
#ifdef G_PASTE_ENABLE_LIBSECRET
        else if (g_paste_storage_keyring_apply_verified (storage_kind, settings))
            self->current_passphrase = g_paste_passphrase_new (g_paste_storage_backend_get_passphrase ());
#endif
        else
        {
            unlock_prompt (prompt, settings, storage_kind, on_rekey_source_unlocked, self);
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
