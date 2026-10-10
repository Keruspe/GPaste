// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-error.h>
#include <gpaste-3/gpaste-gsettings-keys.h>
#include <gpaste-3/gpaste-util.h>

#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-file-backend.h>
#include <gpaste-daemon/gpaste-noop-backend.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-storage-migration.h>

#ifdef G_PASTE_ENABLE_LIBSECRET
#include <gpaste-daemon/gpaste-storage-keyring.h>
#endif

typedef struct
{
    GPasteSettings                *settings;
    GPastePrompt                  *prompt;
    GTask                         *task;

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
 * files on disk wins, so an existing setup is still recognised.
 *
 * The answer is @current, and %FALSE means there is none: the files could not be
 * read at all, and @current is then left as the caller found it. "There is
 * nothing stored" and "we could not look" are the same count and a very
 * different answer, and what comes out of here is written to storage-backend. */
static gboolean
detect_current_backend (GPasteSettings *settings,
                        GPasteStorage  *current)
{
    /* The storing flavours in tie-break precedence order: encrypted over plain,
     * then the encrypted file over the encrypted database but the plain
     * database over the plain file. Extensions come from
     * g_paste_storage_get_extension(), so a new flavour only needs adding here.
     *
     * Every one of them, whether or not this build can open it: what is on disk
     * is what the history is, and what can be done with it is the prompt's
     * rules' business (g_paste_prompt_can_import() and
     * g_paste_prompt_backend_changes() refuse a source this build cannot open).
     * Detected as the plain file such a build degrades it to, or as nothing,
     * it would be recorded as that in storage-backend, and forgotten by the
     * build that can open it. Only a build lacking a feature can tell this list
     * from one gated on the features built in: with every feature, the two are
     * the same. */
    static const GPasteStorage flavours[] = {
        G_PASTE_STORAGE_ENCRYPTED_FILE,
        G_PASTE_STORAGE_ENCRYPTED_SQLITE,
        G_PASTE_STORAGE_SQLITE,
        G_PASTE_STORAGE_FILE,
    };

    const gchar *name = g_paste_settings_get_history_name (settings);

    /* The active history's own file wins, in the precedence order above, so an
     * existing encrypted setup is never demoted. */
    for (guint i = 0; i < G_N_ELEMENTS (flavours); ++i)
    {
        if (g_paste_util_history_is_stored (name, flavours[i]))
        {
            *current = flavours[i];

            return TRUE;
        }
    }

    /* No history under the active name: fall back to whichever flavour has the
     * most files on disk, ties broken by the same precedence. */
    g_autoptr (GStrvBuilder) suffix_builder = g_strv_builder_new ();
    guint counts[G_N_ELEMENTS (flavours)] = { 0 };

    for (guint i = 0; i < G_N_ELEMENTS (flavours); ++i)
        g_strv_builder_take (suffix_builder, g_strconcat (".", g_paste_storage_get_extension (flavours[i]), NULL));

    g_auto (GStrv) suffixes = g_strv_builder_end (suffix_builder);
    g_autoptr (GFile) dir = g_paste_util_get_history_dir ();
    g_autoptr (GError) error = NULL;
    g_auto (GStrv) files = g_paste_util_list_directory (dir, G_FILE_ATTRIBUTE_STANDARD_NAME, &error);

    /* A fresh profile has no directory yet and lists nothing, which is an
     * answer. A directory that is there but unreadable is not: its count would
     * come out zero and be written to storage-backend as "nothing is stored",
     * so it answers nothing at all instead. */
    if (!files)
    {
        g_warning ("Could not list the history directory to detect the current backend: %s", error->message);

        return FALSE;
    }

    for (GStrv file = files; *file; ++file)
    {
        /* The extensions are mutually non-suffixing (".xml" never matches
         * ".xmls", ".db" never ".dbs"), so each file counts for at most one. */
        for (guint i = 0; i < G_N_ELEMENTS (flavours); ++i)
        {
            if (g_str_has_suffix (*file, suffixes[i]))
            {
                ++counts[i];
                break;
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

    *current = counts[best] ? flavours[best] : G_PASTE_STORAGE_NOOP;

    return TRUE;
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* The passphrase that already opens @storage_kind, without asking anyone: the
 * one this process holds, and failing that the one remembered in the keyring,
 * which is applied to the process when it verifies. %NULL when neither does and
 * the user has to be prompted.
 *
 * Everything that needs to unlock an existing history asks here, so they cannot
 * disagree about that order. */
static const gchar *
known_passphrase_for (GPasteStorage   storage_kind,
                      GPasteSettings *settings)
{
    const gchar *known = g_paste_storage_backend_get_passphrase ();

    if (known && g_paste_storage_passphrase_can_decrypt (storage_kind, settings, known))
        return known;

#ifdef G_PASTE_ENABLE_LIBSECRET
    if (g_paste_storage_keyring_apply_verified (storage_kind, settings))
        return g_paste_storage_backend_get_passphrase ();
#else
    (void) settings;
#endif

    return NULL;
}
#endif

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* Act on what the user said about remembering @cleartext. Only ever called
 * where the passphrase has just been proven, which is what makes it safe: a
 * wrong one is never written, and turning the switch off drops whatever was
 * remembered before. */
static void
remember_passphrase (GPasteStorageRemember remember,
                     const gchar          *cleartext)
{
#ifdef G_PASTE_ENABLE_LIBSECRET
    if (remember == G_PASTE_STORAGE_REMEMBER_YES)
        g_paste_storage_keyring_store (cleartext);
    else if (remember == G_PASTE_STORAGE_REMEMBER_NO)
        g_paste_storage_keyring_clear ();
#else
    (void) remember;
    (void) cleartext;
#endif
}
#endif

/* Hand a concern's outcome to whoever started it: the failure that ended it, or
 * plain success -- a dismissal included, since what the user chose has already
 * been applied by the time this runs. Shared by all three, which is what keeps
 * them from disagreeing about what a %TRUE means. */
static void
storage_concern_return (GTask  *task,
                        GError *error) /* (transfer full) (nullable) */
{
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
}

/* One shape for the three _finish(): the source tag is what keeps one concern's
 * result from being read as another's. A mismatch is a wiring mistake rather
 * than anything the user can cause, but it still comes back as a #GError --
 * every caller prints the message it gets, so leaving @error unset would turn
 * the diagnostic into a crash. */
static gboolean
storage_concern_finish (GAsyncResult *result,
                        gpointer      source_tag,
                        GError      **error)
{
    if (!G_IS_TASK (result) || g_task_get_source_tag (G_TASK (result)) != source_tag)
    {
        g_set_error_literal (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT,
                             "This is not the result of the storage concern being finished");

        return FALSE;
    }

    return g_task_propagate_boolean (G_TASK (result), error);
}

/* Release the state and hand control back to the caller: the one way out of the
 * migration, whether it applied, was dismissed or never got started. @error is
 * the prompt failure that ended it, if any -- a dismissal is not one, and a
 * failure always ends the concern where it happened, so there is only ever the
 * one to report. */
static void
migration_finish (MigrationData *self,
                  GError        *error) /* (transfer full) (nullable) */
{
    g_autoptr (GTask) task = self->task;

#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_paste_passphrase_free (self->source_passphrase);
#endif

    g_object_unref (self->settings);
    g_object_unref (self->prompt);
    g_free (self);

    storage_concern_return (task, error);
}

G_PASTE_VISIBLE gboolean
g_paste_storage_migration_needed (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), FALSE);

    return g_paste_settings_get_storage_backend_revision (settings) != G_PASTE_STORAGE_BACKEND_REVISION;
}

/* Whether @written faithfully reproduces @source: same kind, same content. The
 * real value carries that for every kind, an image's being its checksum, which
 * is the same string whichever backend the bytes came back from. This
 * deliberately differs from g_paste_item_equals(): that dedup predicate ignores
 * the kind and treats two distinct passwords as never equal, whereas migration
 * must confirm a password's real value round-tripped (an encrypted backend
 * persists it) — so it is not a drop-in replacement here. */
static gboolean
imported_item_matches (GPasteItem *source,
                       GPasteItem *written)
{
    if (g_paste_item_get_kind (source) != g_paste_item_get_kind (written))
        return FALSE;

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
 * while a path-based one does not).
 *
 * A failure takes back what it already wrote to @chosen, with @error saying
 * which history stopped it: the next start detects the current backend from
 * the files on disk, the active history's first and the flavours in a fixed
 * order (see detect_current_backend()). A copy left behind under a flavour
 * that comes first would be taken for the history in use, every entry copied
 * since the failure lost behind it. */
static gboolean
import_histories (GPasteSettings *settings,
                  GPasteStorage   current,
                  const gchar    *current_passphrase,
                  GPasteStorage   chosen,
                  const gchar    *chosen_passphrase,
                  GError        **error)
{
    /* Both keys are passed explicitly: source and destination may both be
     * encrypted under two different passphrases, which the single process-wide
     * one cannot express. */
    g_autoptr (GPasteStorageBackend) previous = g_paste_storage_backend_new_with_passphrase (current, settings, current_passphrase);
    g_autoptr (GPasteStorageBackend) next = g_paste_storage_backend_new_with_passphrase (chosen, settings, chosen_passphrase);
    g_autoptr (GError) list_error = NULL;
    g_auto (GStrv) names = g_paste_storage_backend_list_histories (previous, &list_error);
    /* Only the plain flavors drop password items; an encrypted destination stores
     * them, so a password missing from the read-back is a genuine write failure
     * there and must not be waved through (the caller may then delete the source). */
    const gboolean chosen_stores_passwords = g_paste_storage_is_encrypted (chosen);
    gboolean ok = TRUE;

    /* A source we could not even list is not an empty one: importing nothing and
     * reporting success would let the caller delete histories still on disk. */
    if (!names)
    {
        g_propagate_prefixed_error (error, g_steal_pointer (&list_error),
                                    "Could not list the histories to migrate: ");
        return FALSE;
    }

    /* Past the last history written to @chosen. */
    GStrv written_end = names;

    for (GStrv name = names; ok && *name; ++name)
    {
        g_autolist (GPasteItem) history = NULL;
        gsize size = 0;

        /* Never let a source we could not actually read (a wrong passphrase, a
         * transient I/O error) pass as an empty history: writing that "empty"
         * into the destination and reporting success would let the caller delete
         * the still-intact originals. Bail out instead so the migration is kept
         * for a retry with the data untouched. */
        if (!g_paste_storage_backend_read_history (previous, *name, NULL /* cancellable */, &history, &size))
        {
            g_set_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED,
                         "Could not read the history “%s”", *name);
            ok = FALSE;
            break;
        }

        /* A destination already holding this history and unable to read it --
         * written by a newer GPaste, under another passphrase -- is one a write
         * refuses to touch, and the rollback below would then delete it all the
         * same: the import stops before it, leaving it exactly as it is. */
        g_autofree gchar *destination = g_paste_storage_backend_get_history_file_path (next, *name);

        if (g_file_test (destination, G_FILE_TEST_EXISTS))
        {
            g_autolist (GPasteItem) existing = NULL;
            gsize existing_size = 0;

            if (!g_paste_storage_backend_read_history (next, *name, NULL /* cancellable */, &existing, &existing_size))
            {
                g_set_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED,
                             "The new storage backend already holds a history “%s” it cannot read", *name);
                ok = FALSE;
                break;
            }
        }

        g_paste_storage_backend_write_history (next, *name, history);
        written_end = name + 1;

        g_autolist (GPasteItem) written = NULL;
        gsize written_size = 0;

        /* Unreadable is what a truncated or undecryptable destination answers,
         * with whatever it got through before stopping, which may well be every
         * item: matching those would let the caller delete the originals of a
         * copy no read will accept. */
        if (!g_paste_storage_backend_read_history (next, *name, NULL /* cancellable */, &written, &written_size))
        {
            g_set_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED,
                         "The history “%s” could not be read back once written", *name);
            ok = FALSE;
            break;
        }

        const GList *w = written;

        for (const GList *h = history; ok && h; h = g_list_next (h))
        {
            if (w && g_paste_str_equal (g_paste_item_get_uuid (h->data), g_paste_item_get_uuid (w->data)))
            {
                ok = imported_item_matches (h->data, w->data);
                w = g_list_next (w);
            }
            else
                ok = !chosen_stores_passwords && g_paste_item_get_kind (h->data) == G_PASTE_ITEM_KIND_PASSWORD;
        }

        /* Nothing may read back that was not written. */
        if (w)
            ok = FALSE;

        if (!ok)
            g_set_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED,
                         "The history “%s” did not read back as it was written", *name);
    }

    if (ok)
        return TRUE;

    /* The destination's image directory goes only once no flavour stores the
     * name, which the source still does: that is the base delete_history()'s
     * rule, and it keeps the source's own images. */
    for (GStrv name = names; name != written_end; ++name)
    {
        g_autoptr (GError) delete_error = NULL;

        g_paste_storage_backend_delete_history (next, *name, &delete_error);

        if (delete_error)
            g_warning ("Could not remove the incomplete copy of the history “%s”: %s", *name, delete_error->message);
    }

    return FALSE;
}

/* Delete what the import copied out of. %FALSE, with @error saying what first
 * went wrong, when any of it is still there: the user asked for their old
 * clipboard history to be deleted, and everything past this point (the revision
 * bump included) treats the migration as done, so this is the last chance to say
 * that part of it was not. */
static gboolean
cleanup_histories (GPasteSettings *settings,
                   GPasteStorage   current,
                   const gchar    *current_passphrase,
                   GError        **error)
{
    g_autoptr (GPasteStorageBackend) previous = g_paste_storage_backend_new_with_passphrase (current, settings, current_passphrase);
    g_autoptr (GError) failure = NULL;
    g_auto (GStrv) names = g_paste_storage_backend_list_histories (previous, &failure);

    if (!names)
    {
        g_propagate_prefixed_error (error, g_steal_pointer (&failure),
                                    "Could not list the migrated histories to clean up: ");

        return FALSE;
    }

    /* Every history that can go, goes: stopping at the first failure would leave
     * behind data that had nothing wrong with it. */
    for (GStrv name = names; *name; ++name)
    {
        g_autoptr (GError) delete_error = NULL;

        g_paste_storage_backend_delete_history (previous, *name, &delete_error);

        if (!delete_error)
            continue;

        g_warning ("Could not delete the migrated history \"%s\": %s", *name, delete_error->message);

        if (!failure)
            failure = g_steal_pointer (&delete_error);
    }

    if (!failure)
        return TRUE;

    g_propagate_error (error, g_steal_pointer (&failure));

    return FALSE;
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
    g_autoptr (GError) error = NULL;

    if (import_histories (work->settings,
                          work->current,
                          g_paste_passphrase_peek (work->current_passphrase),
                          work->chosen,
                          g_paste_passphrase_peek (work->chosen_passphrase),
                          &error))
        g_task_return_boolean (task, TRUE);
    else
        g_task_return_error (task, g_steal_pointer (&error));
}

/* Everything after the import: settings, cleanup and keyring, all back on the
 * main thread where their ordering against the rest of the daemon holds. */
static void
apply_migration_settle (MigrationData *self,
                        gboolean       imported,
                        const GError  *import_error)
{
    GPasteStorage chosen = self->chosen;
    gboolean import = self->import;
    gboolean cleanup = self->cleanup;
    /* The source keeps the key it was unlocked with, the destination gets the
     * one just set (the process-wide passphrase), so an encrypted -> encrypted
     * migration re-keys the history instead of reading the old data with the
     * new key. Both %NULL for the plain flavors, which need none. */
    const gchar *current_passphrase = NULL;

#ifdef G_PASTE_ENABLE_ENCRYPTION
    current_passphrase = g_paste_passphrase_peek (self->source_passphrase);
#endif
#if defined(G_PASTE_ENABLE_ENCRYPTION) && defined(G_PASTE_ENABLE_LIBSECRET)
    /* Only ever read to decide what to remember, which is the one thing that
     * needs the keyring. */
    const gchar *chosen_passphrase = g_paste_storage_backend_get_passphrase ();
#endif

    /* Whether the history moved to @chosen. Not when the import genuinely
     * failed, nor when GSettings refused to store the new backend (a locked
     * key): the daemon then reads the backend the setting still names, and the
     * old data is all it has. Either way keep the current backend, data and
     * revision untouched, so nothing is lost or hidden and the migration is
     * offered again next time rather than silently leaving an empty or unread
     * backend. A failed import has already taken back what it wrote to
     * @chosen; a complete one refused by the lock stays where it is, the
     * same lock refusing the detection that could prefer it. A reply naming
     * the backend the setting already holds reads back equal under a lock
     * too, so it still settles -- "Keep as is", normally: the way out of a
     * dialog that would otherwise come back at every start. */
    gboolean moved = FALSE;

    if (import && !imported)
    {
        g_autofree gchar *message = g_strconcat (g_paste_prompt_text (G_PASTE_PROMPT_TEXT_IMPORT_FAILED_DESCRIPTION),
                                                 "\n\n", import_error->message, NULL);

        g_warning ("History import failed; keeping the current storage backend and old data: %s", import_error->message);
        /* As with a failed cleanup below: the dialog has closed, and the user
         * would otherwise find it back at the next start with no idea why. */
        g_paste_prompt_report (self->prompt,
                               g_paste_prompt_text (G_PASTE_PROMPT_TEXT_IMPORT_FAILED_TITLE),
                               message);
    }
    else
    {
        g_paste_settings_set_storage_backend (self->settings, chosen);
        g_paste_settings_sync_key (self->settings, G_PASTE_STORAGE_BACKEND_SETTING);
        moved = (g_paste_settings_get_storage_backend (self->settings) == chosen);

        if (!moved)
            g_warning ("Could not store the new storage backend; keeping the current one and old data");
    }

    if (!moved)
    {
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
        /* Whether the old data is actually gone, which is not the same as having
         * been told to delete it: the keyring decision below turns on the former. */
        gboolean cleaned = FALSE;

        /* Only delete the old data once the import is confirmed, so a failed
         * import can never wipe the history it was meant to migrate. */
        if (cleanup && imported)
        {
            g_autoptr (GError) cleanup_error = NULL;

            /* Told, not retried: the revision is bumped either way, because the
             * dialog cannot offer this again -- the backend it would come back
             * on is the one we just moved to, and "delete the old data" is only
             * offered for a migration that changes backend. So the user hears
             * that their old clipboard history is still on disk, once, while
             * they are still looking. */
            cleaned = cleanup_histories (self->settings, self->current, current_passphrase, &cleanup_error);

            if (!cleaned)
            {
                g_autofree gchar *message = g_strconcat (g_paste_prompt_text (G_PASTE_PROMPT_TEXT_CLEANUP_FAILED_DESCRIPTION),
                                                         "\n\n", cleanup_error->message, NULL);

                g_paste_prompt_report (self->prompt,
                                       g_paste_prompt_text (G_PASTE_PROMPT_TEXT_CLEANUP_FAILED_TITLE),
                                       message);
            }
        }

        g_paste_settings_set_storage_backend_revision (self->settings, G_PASTE_STORAGE_BACKEND_REVISION);

#if defined(G_PASTE_ENABLE_ENCRYPTION) && defined(G_PASTE_ENABLE_LIBSECRET)
        /* The new passphrase now opens what is actually on disk, so this is when
         * the user's choice about remembering it can be honoured. Only when one
         * was asked for *and* the destination is still the encrypted backend it
         * was asked for: anything else never offered the choice, and must not
         * silently discard an entry that still unlocks a history. */
        if (self->passphrase_prompted && g_paste_storage_is_encrypted (chosen))
        {
            remember_passphrase (self->remember_passphrase, chosen_passphrase);
        }
        /* Leaving encryption behind for good: once the encrypted history it
         * opened is gone, a remembered passphrase is a secret kept for nothing.
         * Only once it is actually gone, though — data we kept is still locked
         * with it, and migrating back would want it. Which is why this asks
         * @cleaned rather than what the user ticked: a cleanup that failed
         * leaves those .xmls files exactly where they were, and dropping the
         * only passphrase that opens them would finish what it could not. */
        else if (g_paste_storage_is_encrypted (self->current) && !g_paste_storage_is_encrypted (chosen) &&
                 cleaned)
            g_paste_storage_keyring_clear ();
#endif
    }

    migration_finish (self, NULL);
}

static void
on_import_done (GObject      *source G_GNUC_UNUSED,
                GAsyncResult *result,
                gpointer      user_data)
{
    g_autoptr (GError) error = NULL;
    gboolean imported = g_task_propagate_boolean (G_TASK (result), &error);

    apply_migration_settle (user_data, imported, error);
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
        apply_migration_settle (self, TRUE, NULL);
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
/* @error is borrowed and only set when the prompt implementation failed, which
 * is not the same as the user dismissing it (@unlocked %FALSE, @error %NULL). */
typedef void (*UnlockDoneFunc) (gboolean  unlocked,
                                GError   *error,
                                gpointer  user_data);

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
    /* A dismissal comes back as %NULL with no error set; an error means the
     * prompt implementation itself failed, which the built-in ones never do but
     * an out-of-tree one (gnome-shell's) can. Both end the same way -- there is
     * no passphrase -- so it is only worth saying which happened. */
    g_autoptr (GError) prompt_error = NULL;
    g_autoptr (GPastePassphrase) passphrase = g_paste_prompt_passphrase_finish (G_PASTE_PROMPT (source), result,
                                                                                &remember, &prompt_error);

    if (prompt_error)
        g_warning ("The passphrase prompt failed: %s", prompt_error->message);

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

        remember_passphrase (remember, cleartext);
    }

    UnlockDoneFunc done = prompt->done;
    gpointer done_data = prompt->user_data;
    gboolean unlocked = (passphrase != NULL);

    g_object_unref (prompt->prompt);
    g_object_unref (prompt->settings);
    g_free (prompt);

    if (done)
        done (unlocked, prompt_error, done_data);
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
    /* As in on_unlock_reply(): a dismissal is %NULL with no error set. */
    g_autoptr (GError) prompt_error = NULL;
    g_autoptr (GPastePassphrase) passphrase = g_paste_prompt_passphrase_finish (G_PASTE_PROMPT (source), result,
                                                                                &remember, &prompt_error);

    /* A prompt that failed outright is not a question the user can answer
     * differently: end the concern on it rather than putting the same dialog
     * back up, which an implementation that fails reliably (a JS exception in an
     * out-of-tree one) would otherwise do for every round of the loop below. */
    if (prompt_error)
    {
        g_warning ("The passphrase prompt failed: %s", prompt_error->message);
        migration_finish (self, g_steal_pointer (&prompt_error));
        return;
    }

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
                    GError  *error,
                    gpointer user_data)
{
    MigrationData *self = user_data;

    /* As in on_passphrase_set(): a prompt that failed cannot be re-asked. */
    if (error)
    {
        migration_finish (self, g_error_copy (error));
        return;
    }

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
        const gchar *known = known_passphrase_for (self->current, self->settings);

        if (!known)
        {
            unlock_prompt (self->prompt, self->settings, self->current, on_source_unlocked, self);
            return;
        }

        set_source_passphrase (self, known);
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
 * deliberate choice, and the detected current backend (written by
 * g_paste_storage_migration_async() below) is what this session keeps in the
 * meantime. */
static void
on_migration_reply (GObject      *source,
                    GAsyncResult *result,
                    gpointer      user_data)
{
    MigrationData *self = user_data;

    /* As for the passphrase prompt: %FALSE is a dismissal, an error is the
     * implementation failing. Either way there is no answer to act on. */
    g_autoptr (GError) prompt_error = NULL;

    if (!g_paste_prompt_migration_finish (G_PASTE_PROMPT (source), result,
                                          &self->chosen, &self->import, &self->cleanup, &prompt_error))
    {
        if (prompt_error)
            g_warning ("The migration prompt failed: %s", prompt_error->message);

        migration_finish (self, g_steal_pointer (&prompt_error));
        return;
    }

    /* The storage chosen is one the prompt was offered:
     * g_paste_prompt_request_reply_migration (), which every prompt answers
     * through, dismisses any other (/storage/migration/unoffered-choice).
     *
     * Both toggles are clamped rather than trusted: a prompt backend is a
     * public, introspectable interface, and "delete the old data" answered for
     * a migration that is not moving anywhere would wipe the current history
     * with nothing imported to replace it -- as would one answered for a format
     * this build cannot open, through the backend standing in for it. Clamping
     * the fields, as the answer comes in, is also what keeps continue_apply()
     * from making the user unlock an encrypted history for an import and a
     * cleanup that will not happen. */
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
 * g_paste_storage_migration_async:
 * @prompt: the #GPastePrompt to ask through
 * @settings: a #GPasteSettings instance
 * @callback: (scope async): called once the migration settled or was dismissed
 * @user_data: data passed to @callback
 *
 * Ask the user where GPaste should store the history, and act on the answer.
 */
G_PASTE_VISIBLE void
g_paste_storage_migration_async (GPastePrompt       *prompt,
                                 GPasteSettings     *settings,
                                 GAsyncReadyCallback callback,
                                 gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_PROMPT (prompt));
    g_return_if_fail (G_PASTE_IS_SETTINGS (settings));

    /* Detect the backend the history currently lives in from the files on disk,
     * and apply it right away so this session keeps the right backend even if the
     * prompt is dismissed without an explicit choice (importantly, an encrypted
     * history is not silently downgraded to "none").
     *
     * A detection that could not read the disk writes nothing: the stored setting
     * is then the best thing we know, and overwriting it with a guess is how an
     * encrypted history gets recorded as "no storage" and stops being offered for
     * import. */
    GPasteStorage current = g_paste_settings_get_storage_backend (settings);

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* Ahead of the import that may read one, as in
     * g_paste_storage_decryption_needed(). */
    g_paste_file_backend_settle_rekeys ();
#endif

    /* Read back, rather than taken as detected: a storage-backend GSettings
     * refuses to store -- a locked key -- leaves the daemon on the backend it
     * holds, and that one is what an import reads and a cleanup deletes. */
    if (detect_current_backend (settings, &current))
    {
        g_paste_settings_set_storage_backend (settings, current);
        g_paste_settings_sync_key (settings, G_PASTE_STORAGE_BACKEND_SETTING);
        current = g_paste_settings_get_storage_backend (settings);
    }

    MigrationData *self = g_new0 (MigrationData, 1);

    self->settings = g_object_ref (settings);
    self->prompt = g_object_ref (prompt);
    self->task = g_task_new (prompt, NULL /* cancellable */, callback, user_data);
    self->current = current;

    /* The three concerns hand their results back through the same task type, so
     * the tag is what keeps one from being read as another. */
    g_task_set_source_tag (self->task, g_paste_storage_migration_async);

    ask_migration (self);
}

/**
 * g_paste_storage_migration_finish:
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Collect the outcome of g_paste_storage_migration_async().
 *
 * The only failure is the #GPastePrompt implementation itself, so the domain is
 * whichever one that reports in: the built-in prompts never fail, an
 * out-of-tree one may raise anything (%G_PASTE_ERROR, %G_IO_ERROR,
 * %G_DBUS_ERROR, ...). Handing in the result of another operation is
 * %G_PASTE_ERROR.
 *
 * Returns: %TRUE unless the prompt implementation failed outright
 */
G_PASTE_VISIBLE gboolean
g_paste_storage_migration_finish (GAsyncResult *result,
                                  GError      **error)
{
    return storage_concern_finish (result, g_paste_storage_migration_async, error);
}

/**
 * g_paste_storage_decryption_needed:
 * @settings: a #GPasteSettings instance
 *
 * Whether an encrypted history still needs decrypting before it can be loaded:
 * the backend is encrypted and no passphrase is set yet. When libsecret is
 * built, a keyring passphrase that unlocks the history is applied as a side
 * effect (answering %FALSE then), so %TRUE means a prompt is genuinely
 * required.
 *
 * Every start asks this before loading anything, so it is also where the store
 * is made fit to be read, whatever its flavour: a change of passphrase the last
 * process left unfinished is settled, and, once per process, the images GPaste
 * 50 kept in the history directory's shared images/ that no history names any
 * more are deleted.
 *
 * Returns: whether the passphrase has to be asked for
 */
G_PASTE_VISIBLE gboolean
g_paste_storage_decryption_needed (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), FALSE);

    /* Every start asks this before loading anything, which is what the sweep
     * needs: see g_paste_file_backend_sweep_legacy_images (). Once per process,
     * gnome-shell asking again before an in-place migration, with a history
     * loaded. */
    static gsize legacy_images_swept = 0;

    if (g_once_init_enter (&legacy_images_swept))
    {
        g_paste_file_backend_sweep_legacy_images ();
        g_once_init_leave (&legacy_images_swept, 1);
    }

#ifdef G_PASTE_ENABLE_ENCRYPTION
    GPasteStorage storage_kind = g_paste_settings_get_storage_backend (settings);

    /* Every start asks this before loading anything or verifying a passphrase,
     * which is when a passphrase change the last process did not finish has
     * to be put back or done with: a history it left half re-keyed refuses
     * every read until then. */
    g_paste_file_backend_settle_rekeys ();

    /* Only an encrypted history that is not already unlocked needs decrypting,
     * and only in a build that can open it: one that cannot keeps nothing at
     * all instead, and could verify no passphrase -- every one would be
     * accepted, then used for nothing. */
    if (!g_paste_storage_is_encrypted (storage_kind) ||
        !g_paste_prompt_storage_is_offered (storage_kind) ||
        g_paste_storage_backend_get_passphrase ())
        return FALSE;

#ifdef G_PASTE_ENABLE_LIBSECRET
    /* A keyring passphrase that unlocks the history is applied here, so no prompt
     * is needed at all: the caller can load straight away with the passphrase now
     * set in this process. */
    if (g_paste_storage_keyring_apply_verified (storage_kind, settings))
        return FALSE;
#endif

    return TRUE;
#else
    return FALSE;
#endif
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* The caller carries on either way: an encrypted history that stayed locked
 * loads as unreadable, which the history already refuses to overwrite. So only
 * a prompt that failed outright is worth reporting. */
static void
on_decryption_settled (gboolean unlocked G_GNUC_UNUSED,
                       GError  *error,
                       gpointer user_data)
{
    g_autoptr (GTask) task = user_data;

    storage_concern_return (task, (error) ? g_error_copy (error) : NULL);
}
#endif

/**
 * g_paste_storage_decryption_async:
 * @prompt: the #GPastePrompt to ask through
 * @settings: a #GPasteSettings instance
 * @callback: (scope async): called once the history is unlocked or the prompt dismissed
 * @user_data: data passed to @callback
 *
 * Unlock an already-encrypted history through a passphrase prompt.
 */
G_PASTE_VISIBLE void
g_paste_storage_decryption_async (GPastePrompt       *prompt,
                                  GPasteSettings     *settings,
                                  GAsyncReadyCallback callback,
                                  gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_PROMPT (prompt));
    g_return_if_fail (G_PASTE_IS_SETTINGS (settings));

    GTask *task = g_task_new (prompt, NULL /* cancellable */, callback, user_data);

    g_task_set_source_tag (task, g_paste_storage_decryption_async);

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* The task is the callback's user_data, and on_decryption_settled owns it. */
    unlock_prompt (prompt, settings, g_paste_settings_get_storage_backend (settings),
                   on_decryption_settled, task);
#else
    (void) settings;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
#endif
}

/**
 * g_paste_storage_decryption_finish:
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Collect the outcome of g_paste_storage_decryption_async().
 *
 * As for g_paste_storage_migration_finish(): the only failure is the
 * #GPastePrompt implementation itself, in whichever domain it reports
 * (%G_PASTE_ERROR, %G_IO_ERROR, %G_DBUS_ERROR, ...), and handing in the result
 * of another operation is %G_PASTE_ERROR.
 *
 * Returns: %TRUE unless the prompt implementation failed outright
 */
G_PASTE_VISIBLE gboolean
g_paste_storage_decryption_finish (GAsyncResult *result,
                                   GError      **error)
{
    return storage_concern_finish (result, g_paste_storage_decryption_async, error);
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
typedef struct
{
    GPastePrompt                  *prompt;
    GPasteSettings                *settings;
    GTask                         *task;

    /* The encrypted flavor being re-keyed, and the passphrase it is currently
     * encrypted with once we have one that actually decrypts it (in gcr secure
     * memory). */
    GPasteStorage                  storage_kind;
    GPastePassphrase              *current_passphrase;

    /* What the worker is re-keying to, and what to do about remembering it
     * once it has — both settled before the hop, acted on after. */
    GPastePassphrase              *new_passphrase;
    GPasteStorageRemember          remember;

    /* Set by the worker when a failed re-key could not be undone either, so the
     * histories are left split across two passphrases. Written on the worker
     * thread and read once it has completed, which the task's completion orders
     * for us. It picks which failure the user is told about: "nothing changed"
     * would be a lie here, and the reassuring one. */
    gboolean                       split;
} RekeyData;

/* As migration_finish(): the one way out, carrying whatever ended it. */
static void
rekey_finish (RekeyData *self,
              GError    *error) /* (transfer full) (nullable) */
{
    g_autoptr (GTask) task = self->task;

    g_paste_passphrase_free (self->current_passphrase);
    g_paste_passphrase_free (self->new_passphrase);
    g_object_unref (self->settings);
    g_object_unref (self->prompt);
    g_free (self);

    storage_concern_return (task, error);
}

/* Re-key @name on its own backend: a backend caches whatever it last opened, so
 * giving each history a fresh one keeps a re-keyed database from being reopened
 * through an instance that still holds the old key. */
static gboolean
rekey_history (RekeyData   *self,
               const gchar *name,
               const gchar *from,
               const gchar *to,
               gboolean    *split)
{
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (self->storage_kind,
                                                                                            self->settings,
                                                                                            from);

    return g_paste_storage_backend_rekey (backend, name, to, split);
}

/* Re-encrypt every history of the current flavor with @passphrase. Each one is
 * re-keyed whole or put back as it was, short of the filesystem refusing even
 * that, but they go one at a time: stop at the first failure rather than
 * carrying on, so what is left is "these are on the new passphrase, those are
 * still on the old" — recoverable by running again — instead of a set silently
 * split further apart. */
static gboolean
rekey_histories (RekeyData   *self,
                 const gchar *passphrase,
                 GError     **error)
{
    const gchar *current = g_paste_passphrase_peek (self->current_passphrase);
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (self->storage_kind,
                                                                                            self->settings,
                                                                                            current);
    g_autoptr (GError) list_error = NULL;
    g_auto (GStrv) names = g_paste_storage_backend_list_histories (backend, &list_error);

    /* Not "no histories" (that is an empty list): the listing itself failed, so
     * we have no idea what would be left behind. */
    if (!names)
    {
        g_propagate_prefixed_error (error, g_steal_pointer (&list_error),
                                    "Could not list the histories to change their passphrase: ");

        return FALSE;
    }

    for (GStrv name = names; *name; ++name)
    {
        gboolean split = FALSE;

        if (rekey_history (self, *name, current, passphrase, &split))
            continue;

        /* The failing one has put back what it had changed, unless it says it
         * could not: then it is on both passphrases whatever happens to the
         * others, which is the failure the user has to hear about. */
        if (split)
        {
            g_warning ("The history \"%s\" is left partly on the new passphrase", *name);
            self->split = TRUE;
        }

        /* Put the ones that did move back on the passphrase they all still
         * shared a moment ago, rather than leave the set split between two — a
         * split one cannot be repaired by running this again, since it starts
         * from a single passphrase. */
        for (GStrv done = names; done != name; ++done)
        {
            if (rekey_history (self, *done, passphrase, current, NULL))
                continue;

            g_warning ("The history \"%s\" is left on the new passphrase while the others keep the old one", *done);
            self->split = TRUE;
        }

        g_set_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED,
                     "Failed to change the passphrase of the history \"%s\"", *name);

        return FALSE;
    }

    /* An encrypted backend with nothing stored under it is not something we can
     * meaningfully re-key, and it is also what a flavor this build cannot
     * construct degrades to — adopting a passphrase then, and remembering it,
     * would hand out a key that opens nothing. */
    if (!*names)
    {
        g_set_error_literal (error, G_PASTE_ERROR, G_PASTE_ERROR_NOT_FOUND,
                             "There is no stored history to change the passphrase of");

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
    g_autoptr (GError) error = NULL;

    /* Unlike the import worker this one reports why: a re-key that did not
     * happen leaves the user holding a passphrase their history does not take,
     * which is the one outcome they have to be told about. */
    if (rekey_histories (self, g_paste_passphrase_peek (self->new_passphrase), &error))
        g_task_return_boolean (task, TRUE);
    else
        g_task_return_error (task, g_steal_pointer (&error));
}

/* Back on the main thread: only adopt the new passphrase once the data on disk
 * actually speaks it, or the daemon would reload an unreadable history — and
 * only remember it then, for the same reason. A re-key that gave up part way
 * leaves the keyring alone: it still holds the passphrase the untouched
 * histories take, and its failure is what the caller is told about. */
static void
on_rekey_done (GObject      *source G_GNUC_UNUSED,
               GAsyncResult *result,
               gpointer      user_data)
{
    RekeyData *self = user_data;
    g_autoptr (GError) error = NULL;

    if (g_task_propagate_boolean (G_TASK (result), &error))
    {
        const gchar *cleartext = g_paste_passphrase_peek (self->new_passphrase);

        g_paste_storage_backend_set_passphrase (cleartext);

        remember_passphrase (self->remember, cleartext);
    }
    else
    {
        /* The user typed a new passphrase, watched the dialog close, and their
         * history still takes the old one. Nothing downstream can tell them
         * that: the caller only gets a %FALSE it can log, and the D-Bus request
         * that asked for the change was answered before any of this ran. The
         * worker never fails without saying why (see rekey_work_run), so there
         * is always a reason to show.
         *
         * Which of the two failures it was matters more than the reason: a
         * re-key that could not be undone leaves histories on both passphrases,
         * and telling that user "nothing changed" would send them off with the
         * one passphrase that no longer opens everything. */
        g_autofree gchar *message = g_strconcat (g_paste_prompt_text (self->split
                                                                      ? G_PASTE_PROMPT_TEXT_REKEY_SPLIT_DESCRIPTION
                                                                      : G_PASTE_PROMPT_TEXT_REKEY_FAILED_DESCRIPTION),
                                                 "\n\n", error->message, NULL);

        g_paste_prompt_report (self->prompt,
                               g_paste_prompt_text (G_PASTE_PROMPT_TEXT_REKEY_FAILED_TITLE),
                               message);
    }

    rekey_finish (self, g_steal_pointer (&error));
}

static void
on_rekey_passphrase_set (GObject      *source,
                         GAsyncResult *result,
                         gpointer      user_data)
{
    RekeyData *self = user_data;
    GPasteStorageRemember remember;
    /* As in on_unlock_reply(): a dismissal is %NULL with no error set. */
    g_autoptr (GError) prompt_error = NULL;
    g_autoptr (GPastePassphrase) passphrase = g_paste_prompt_passphrase_finish (G_PASTE_PROMPT (source), result,
                                                                                &remember, &prompt_error);

    if (prompt_error)
    {
        g_warning ("The passphrase prompt failed: %s", prompt_error->message);
        rekey_finish (self, g_steal_pointer (&prompt_error));
        return;
    }

    /* Dismissed: nothing has been touched yet, so there is nothing to undo. */
    if (!passphrase)
    {
        rekey_finish (self, NULL);
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
                          GError  *error,
                          gpointer user_data)
{
    RekeyData *self = user_data;

    /* As in the migration: a prompt that failed cannot be re-asked. */
    if (error)
    {
        rekey_finish (self, g_error_copy (error));
        return;
    }

    /* Dismissed without unlocking: there is no passphrase to change. */
    if (!unlocked)
    {
        rekey_finish (self, NULL);
        return;
    }

    self->current_passphrase = g_paste_passphrase_new (g_paste_storage_backend_get_passphrase ());

    rekey_prompt_new_passphrase (self);
}
#endif

/**
 * g_paste_storage_rekey_async:
 * @prompt: the #GPastePrompt to ask through
 * @settings: a #GPasteSettings instance
 * @callback: (scope async): called once the passphrase was changed, or the prompt dismissed
 * @user_data: data passed to @callback
 *
 * Change the passphrase of the encrypted history, re-encrypting everything with
 * the new one. Unlike the migration dialog this never moves the history: the
 * data stays in the backend it is in, only its key changes.
 *
 * Unlocks the history first (from this process, the keyring, or a prompt), then
 * asks for the new passphrase with a confirmation. Completes immediately when
 * the history is not encrypted, so there is no passphrase to change.
 */
G_PASTE_VISIBLE void
g_paste_storage_rekey_async (GPastePrompt       *prompt,
                             GPasteSettings     *settings,
                             GAsyncReadyCallback callback,
                             gpointer            user_data)
{
    g_return_if_fail (G_PASTE_IS_PROMPT (prompt));
    g_return_if_fail (G_PASTE_IS_SETTINGS (settings));

    GTask *task = g_task_new (prompt, NULL /* cancellable */, callback, user_data);

    g_task_set_source_tag (task, g_paste_storage_rekey_async);

#ifdef G_PASTE_ENABLE_ENCRYPTION
    GPasteStorage storage_kind = g_paste_settings_get_storage_backend (settings);

    if (g_paste_storage_is_encrypted (storage_kind))
    {
        RekeyData *self = g_new0 (RekeyData, 1);

        self->prompt = g_object_ref (prompt);
        self->settings = g_object_ref (settings);
        self->task = task;
        self->storage_kind = storage_kind;

        /* The passphrase this process already holds is the common case (the
         * daemon has been serving the history with it), then the keyring, then
         * ask — the same order the migration unlocks a source with. */
        const gchar *known = known_passphrase_for (storage_kind, settings);

        if (!known)
        {
            unlock_prompt (prompt, settings, storage_kind, on_rekey_source_unlocked, self);
            return;
        }

        self->current_passphrase = g_paste_passphrase_new (known);

        rekey_prompt_new_passphrase (self);

        return;
    }

    g_warning ("The history is not encrypted: there is no passphrase to change");
#else
    (void) settings;
#endif

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/**
 * g_paste_storage_rekey_finish:
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Collect the outcome of g_paste_storage_rekey_async().
 *
 * Unlike the other two concerns this one can fail on its own account: a re-key
 * that could not rewrite the histories on disk reports %G_PASTE_ERROR, and so
 * does handing in the result of another operation. A failing #GPastePrompt
 * implementation reports in whichever domain it uses (%G_PASTE_ERROR,
 * %G_IO_ERROR, %G_DBUS_ERROR, ...).
 *
 * Returns: %TRUE unless the histories could not be re-encrypted, or the prompt
 *          implementation failed outright
 */
G_PASTE_VISIBLE gboolean
g_paste_storage_rekey_finish (GAsyncResult *result,
                              GError      **error)
{
    return storage_concern_finish (result, g_paste_storage_rekey_async, error);
}
