// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-util.h>

#include <gpaste-daemon/gpaste-prompt.h>

#ifdef G_PASTE_ENABLE_LIBSECRET
#include <gpaste-daemon/gpaste-storage-keyring.h>
#endif

#ifdef G_PASTE_ENABLE_PWQUALITY
#include <pwquality.h>
#endif

struct _GPastePromptRequest
{
    GObject parent_instance;
};

/* Which question a request is, so that answering it with the other one's reply
 * is caught rather than read back as a struct of the wrong shape. */

typedef enum
{
    REQUEST_PASSPHRASE,
    REQUEST_MIGRATION,
} RequestKind;

typedef struct
{
    /* Holds the caller's completion, until whoever answers first takes it. */
    GTask                *task;
    RequestKind           kind;

    gboolean              confirm;
    gchar                *error_message;
    GPasteStorageRemember remember;

    GPasteStorage        *offered;
    guint                 n_offered;
    GPasteStorage         current;
} GPastePromptRequestPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (PromptRequest, prompt_request, G_TYPE_OBJECT)

/* What a passphrase prompt came back with, carried through the GTask. */
typedef struct
{
    GPastePassphrase     *passphrase;
    GPasteStorageRemember remember;
} PassphraseReply;

static void
passphrase_reply_free (gpointer data)
{
    PassphraseReply *reply = data;

    g_paste_passphrase_free (reply->passphrase);
    g_free (reply);
}

/* And what a migration prompt came back with. */
typedef struct
{
    GPasteStorage chosen;
    gboolean      import;
    gboolean      cleanup;
} MigrationReply;

static void
g_paste_prompt_request_dispose (GObject *object)
{
    GPastePromptRequestPrivate *priv = g_paste_prompt_request_get_instance_private (G_PASTE_PROMPT_REQUEST (object));

    /* Nobody answered it and nobody kept it. Dismiss rather than drop the task:
     * a backend that loses a request is a bug, but one that leaves the caller
     * waiting forever on a prompt nothing will ever answer is a worse one. */
    if (priv->task)
    {
        g_autoptr (GTask) task = g_steal_pointer (&priv->task);

        g_task_return_pointer (task, NULL, NULL);
    }

    G_OBJECT_CLASS (g_paste_prompt_request_parent_class)->dispose (object);
}

static void
g_paste_prompt_request_finalize (GObject *object)
{
    GPastePromptRequestPrivate *priv = g_paste_prompt_request_get_instance_private (G_PASTE_PROMPT_REQUEST (object));

    g_free (priv->error_message);
    g_free (priv->offered);

    G_OBJECT_CLASS (g_paste_prompt_request_parent_class)->finalize (object);
}

static void
g_paste_prompt_request_class_init (GPastePromptRequestClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_prompt_request_dispose;
    object_class->finalize = g_paste_prompt_request_finalize;
}

static void
g_paste_prompt_request_init (GPastePromptRequest *self G_GNUC_UNUSED)
{
}

static GPastePromptRequest *
g_paste_prompt_request_new (GPastePrompt        *prompt,
                            RequestKind          kind,
                            gpointer             source_tag,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
    GPastePromptRequest *self = G_PASTE_PROMPT_REQUEST (g_object_new (G_PASTE_TYPE_PROMPT_REQUEST, NULL));
    GPastePromptRequestPrivate *priv = g_paste_prompt_request_get_instance_private (self);

    priv->kind = kind;
    priv->task = g_task_new (prompt, NULL, callback, user_data);
    g_task_set_source_tag (priv->task, source_tag);

    return self;
}


/* The source tag is set when the request is made, so it says which question was
 * asked — not which reply is being given. Only the kind can catch a backend
 * answering a migration with a passphrase, which would otherwise complete the
 * task with a reply of the wrong shape for _finish() to read.
 *
 * A mismatch dismisses rather than merely refusing: the backend is buggy either
 * way, but leaving the request unanswered would hang whoever asked, and this is
 * the one place that still knows how to end it. */
static gboolean
g_paste_prompt_request_answers (GPastePromptRequest *self,
                                RequestKind          kind)
{
    const GPastePromptRequestPrivate *priv = _g_paste_prompt_request_get_instance_private (self);

    if (priv->kind == kind)
        return TRUE;

    g_warning ("A prompt answered a request with the wrong kind of reply; dismissing it");
    g_paste_prompt_request_dismiss (self);

    return FALSE;
}

/* Take the task away, so whichever reply gets there first is the one that
 * answers and the rest are no-ops. */
static GTask *
g_paste_prompt_request_claim (GPastePromptRequest *self)
{
    GPastePromptRequestPrivate *priv = g_paste_prompt_request_get_instance_private (self);

    return g_steal_pointer (&priv->task);
}

/**
 * g_paste_prompt_request_get_confirm:
 * @self: a #GPastePromptRequest
 *
 * Whether the passphrase should be asked for twice (a new encrypted history).
 *
 * Returns: whether to confirm the passphrase
 */
G_PASTE_VISIBLE gboolean
g_paste_prompt_request_get_confirm (const GPastePromptRequest *self)
{
    g_return_val_if_fail (_G_PASTE_IS_PROMPT_REQUEST (self), FALSE);

    const GPastePromptRequestPrivate *priv = _g_paste_prompt_request_get_instance_private (self);

    return priv->confirm;
}

/**
 * g_paste_prompt_request_get_error_message:
 * @self: a #GPastePromptRequest
 *
 * An error to show above the entry, e.g. when re-prompting after a wrong
 * passphrase.
 *
 * Returns: (nullable): read-only string, or %NULL when there is nothing to say
 */
G_PASTE_VISIBLE const gchar *
g_paste_prompt_request_get_error_message (const GPastePromptRequest *self)
{
    g_return_val_if_fail (_G_PASTE_IS_PROMPT_REQUEST (self), NULL);

    const GPastePromptRequestPrivate *priv = _g_paste_prompt_request_get_instance_private (self);

    return priv->error_message;
}

/**
 * g_paste_prompt_request_get_remember:
 * @self: a #GPastePromptRequest
 *
 * Where the "Remember this passphrase" switch should start.
 *
 * Returns: the initial state of the switch
 */
G_PASTE_VISIBLE GPasteStorageRemember
g_paste_prompt_request_get_remember (const GPastePromptRequest *self)
{
    g_return_val_if_fail (_G_PASTE_IS_PROMPT_REQUEST (self), G_PASTE_STORAGE_REMEMBER_UNCHANGED);

    const GPastePromptRequestPrivate *priv = _g_paste_prompt_request_get_instance_private (self);

    return priv->remember;
}

/**
 * g_paste_prompt_request_get_offered:
 * @self: a #GPastePromptRequest
 * @n_offered: (out): the number of backends returned
 *
 * The storage backends to offer, in display order.
 *
 * Returns: (array length=n_offered) (transfer none): the backends
 */
G_PASTE_VISIBLE const GPasteStorage *
g_paste_prompt_request_get_offered (const GPastePromptRequest *self,
                                    guint                     *n_offered)
{
    g_return_val_if_fail (n_offered, NULL);

    /* Before the preconditions: @n_offered is not optional, and an introspected
     * caller building an array from it (that is what the annotation says to do)
     * would otherwise read whatever the binding left on the stack — with a NULL
     * pointer to walk that many entries of. */
    *n_offered = 0;

    g_return_val_if_fail (_G_PASTE_IS_PROMPT_REQUEST (self), NULL);

    const GPastePromptRequestPrivate *priv = _g_paste_prompt_request_get_instance_private (self);

    g_return_val_if_fail (priv->kind == REQUEST_MIGRATION, NULL);

    *n_offered = priv->n_offered;

    return priv->offered;
}

/**
 * g_paste_prompt_request_get_current:
 * @self: a #GPastePromptRequest
 *
 * The backend the history currently lives in, which the prompt starts on.
 *
 * Returns: the current backend
 */
G_PASTE_VISIBLE GPasteStorage
g_paste_prompt_request_get_current (const GPastePromptRequest *self)
{
    g_return_val_if_fail (_G_PASTE_IS_PROMPT_REQUEST (self), G_PASTE_STORAGE_NOOP);

    const GPastePromptRequestPrivate *priv = _g_paste_prompt_request_get_instance_private (self);

    return priv->current;
}

/**
 * g_paste_prompt_request_reply_passphrase:
 * @self: a #GPastePromptRequest
 * @passphrase: (nullable): the passphrase the user entered
 * @remember: what the "Remember this passphrase" switch ended up saying
 *
 * Answer a passphrase request.
 *
 * An empty passphrase is no passphrase: it is delivered as %NULL, so callers
 * treat it as a dismissal rather than configuring an unprotected "encrypted"
 * history (they only null-check the pointer, not its contents).
 */
G_PASTE_VISIBLE void
g_paste_prompt_request_reply_passphrase (GPastePromptRequest  *self,
                                         const gchar          *passphrase,
                                         GPasteStorageRemember remember)
{
    g_return_if_fail (_G_PASTE_IS_PROMPT_REQUEST (self));

    if (!g_paste_prompt_request_answers (self, REQUEST_PASSPHRASE))
        return;

    g_autoptr (GTask) task = g_paste_prompt_request_claim (self);

    if (!task)
        return;

    if (passphrase && !*passphrase)
        passphrase = NULL;

    if (!passphrase)
    {
        g_task_return_pointer (task, NULL, NULL);
        return;
    }

    PassphraseReply *reply = g_new0 (PassphraseReply, 1);

    /* Into secure memory the moment it crosses out of the backend: the entry
     * widget's own buffer is ordinary heap and beyond our reach, but everything
     * downstream of here is ours. */
    reply->passphrase = g_paste_passphrase_new (passphrase);
    reply->remember = remember;

    g_task_return_pointer (task, reply, passphrase_reply_free);
}

/**
 * g_paste_prompt_request_reply_migration:
 * @self: a #GPastePromptRequest
 * @chosen: the backend the user picked
 * @import: whether to copy the current history into it
 * @cleanup: whether to delete the previous on-disk history afterwards
 *
 * Answer a migration request.
 */
G_PASTE_VISIBLE void
g_paste_prompt_request_reply_migration (GPastePromptRequest *self,
                                        GPasteStorage        chosen,
                                        gboolean             import,
                                        gboolean             cleanup)
{
    g_return_if_fail (_G_PASTE_IS_PROMPT_REQUEST (self));

    if (!g_paste_prompt_request_answers (self, REQUEST_MIGRATION))
        return;

    /* @chosen is the one answer that selects code paths, and the request is
     * carrying the exact set of legal ones — so check it here, on the single
     * path every backend goes through, rather than downstream where a flavour
     * this build cannot construct would be persisted and then silently degrade
     * to "no storage", leaving the daemon not persisting at all across
     * restarts. The toggles are clamped where they are acted on. */
    const GPastePromptRequestPrivate *priv = _g_paste_prompt_request_get_instance_private (self);
    gboolean offered = FALSE;

    for (guint i = 0; i < priv->n_offered && !offered; ++i)
        offered = (priv->offered[i] == chosen);

    if (!offered)
    {
        g_warning ("A prompt answered the migration with a storage backend that was not offered; dismissing it");
        g_paste_prompt_request_dismiss (self);
        return;
    }

    g_autoptr (GTask) task = g_paste_prompt_request_claim (self);

    if (!task)
        return;

    MigrationReply *reply = g_new0 (MigrationReply, 1);

    reply->chosen = chosen;
    reply->import = import;
    reply->cleanup = cleanup;

    g_task_return_pointer (task, reply, g_free);
}

/**
 * g_paste_prompt_request_dismiss:
 * @self: a #GPastePromptRequest
 *
 * Answer a request with "the user said no", whichever kind it is.
 */
G_PASTE_VISIBLE void
g_paste_prompt_request_dismiss (GPastePromptRequest *self)
{
    g_return_if_fail (_G_PASTE_IS_PROMPT_REQUEST (self));

    g_autoptr (GTask) task = g_paste_prompt_request_claim (self);

    if (task)
        g_task_return_pointer (task, NULL, NULL);
}

G_DEFINE_INTERFACE (GPastePrompt, g_paste_prompt, G_TYPE_OBJECT)

static void
g_paste_prompt_default_prompt (GPastePrompt        *self G_GNUC_UNUSED,
                               GPastePromptRequest *request)
{
    g_paste_prompt_request_dismiss (request);
}

static void
g_paste_prompt_default_init (GPastePromptInterface *iface)
{
    iface->passphrase = g_paste_prompt_default_prompt;
    iface->migration = g_paste_prompt_default_prompt;
}

/**
 * g_paste_prompt_passphrase_async:
 * @self: a #GPastePrompt instance
 * @confirm: whether to ask for the passphrase twice (a new encrypted history)
 * @error_message: (nullable): an error to show above the entry, e.g. when
 *                 re-prompting after a wrong passphrase
 * @remember: where the "Remember this passphrase" switch starts. %UNCHANGED
 *            asks the keyring; anything else carries a choice the user already
 *            made forward — which is what a re-prompt has to do, or it would
 *            silently undo it
 * @callback: (scope async): called once the prompt is answered or dismissed
 * @user_data: data passed to @callback
 *
 * Ask the user for the encrypted history passphrase.
 */
G_PASTE_VISIBLE void
g_paste_prompt_passphrase_async (GPastePrompt         *self,
                                 gboolean              confirm,
                                 const gchar          *error_message,
                                 GPasteStorageRemember remember,
                                 GAsyncReadyCallback   callback,
                                 gpointer              user_data)
{
    g_return_if_fail (_G_PASTE_IS_PROMPT (self));

    /* Ours to drop once the vfunc has had it: a backend that keeps it has
     * referenced it, and one that has not gets a dismissal from dispose. */
    g_autoptr (GPastePromptRequest) request = g_paste_prompt_request_new (self, REQUEST_PASSPHRASE,
                                                                          g_paste_prompt_passphrase_async,
                                                                          callback, user_data);
    GPastePromptRequestPrivate *priv = g_paste_prompt_request_get_instance_private (request);

    priv->confirm = confirm;
    priv->error_message = g_strdup (error_message);
    priv->remember = remember;

    G_PASTE_PROMPT_GET_IFACE (self)->passphrase (self, request);
}

/**
 * g_paste_prompt_passphrase_finish:
 * @self: a #GPastePrompt instance
 * @result: the #GAsyncResult handed to the callback
 * @remember: (out) (optional): what to do about remembering the passphrase.
 *            Acting on it is deliberately the caller's job: only it can tell
 *            whether the passphrase turned out to be the right one, and
 *            remembering a wrong one costs a prompt on every startup until it
 *            is discarded
 * @error: (nullable): a #GError, or %NULL
 *
 * Collect the passphrase the user entered.
 *
 * Returns: (transfer full) (nullable): the passphrase, already in secure
 *          memory, or %NULL if the prompt was dismissed
 */
G_PASTE_VISIBLE GPastePassphrase *
g_paste_prompt_passphrase_finish (GPastePrompt          *self,
                                  GAsyncResult          *result,
                                  GPasteStorageRemember *remember,
                                  GError               **error)
{
    /* Before the preconditions: a caller that trips one still reads it. */
    if (remember)
        *remember = G_PASTE_STORAGE_REMEMBER_UNCHANGED;

    g_return_val_if_fail (_G_PASTE_IS_PROMPT (self), NULL);
    g_return_val_if_fail (g_task_is_valid (result, self), NULL);
    /* The two concerns return different payloads through the same task type, so
     * the tag is what keeps a migration result from being read as a passphrase. */
    g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) == g_paste_prompt_passphrase_async, NULL);

    PassphraseReply *reply = g_task_propagate_pointer (G_TASK (result), error);

    if (!reply)
        return NULL;

    if (remember)
        *remember = reply->remember;

    GPastePassphrase *passphrase = g_steal_pointer (&reply->passphrase);

    g_free (reply);

    return passphrase;
}

/**
 * g_paste_prompt_migration_async:
 * @self: a #GPastePrompt instance
 * @offered: (array length=n_offered): the backends to offer, in display order
 * @n_offered: the number of backends in @offered
 * @current: the backend the history currently lives in, which the prompt starts on
 * @callback: (scope async): called once the prompt is answered or dismissed
 * @user_data: data passed to @callback
 *
 * Ask the user where GPaste should store the history from now on.
 *
 * The prompt is answered once per invocation: gathering whatever passphrases
 * the chosen backend still needs, and asking again when the user amends their
 * choice, is the caller's business.
 */
G_PASTE_VISIBLE void
g_paste_prompt_migration_async (GPastePrompt        *self,
                                const GPasteStorage *offered,
                                guint                n_offered,
                                GPasteStorage        current,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
    g_return_if_fail (_G_PASTE_IS_PROMPT (self));
    g_return_if_fail (offered && n_offered);

    /* Ours to drop once the vfunc has had it: a backend that keeps it has
     * referenced it, and one that has not gets a dismissal from dispose. */
    g_autoptr (GPastePromptRequest) request = g_paste_prompt_request_new (self, REQUEST_MIGRATION,
                                                                          g_paste_prompt_migration_async,
                                                                          callback, user_data);
    GPastePromptRequestPrivate *priv = g_paste_prompt_request_get_instance_private (request);

    priv->offered = g_memdup2 (offered, n_offered * sizeof (GPasteStorage));
    priv->n_offered = n_offered;
    priv->current = current;

    G_PASTE_PROMPT_GET_IFACE (self)->migration (self, request);
}

/**
 * g_paste_prompt_migration_finish:
 * @self: a #GPastePrompt instance
 * @result: the #GAsyncResult handed to the callback
 * @chosen: (out) (optional): the backend the user picked
 * @import: (out) (optional): whether to copy the current history into it
 * @cleanup: (out) (optional): whether to delete the previous on-disk history
 * @error: (nullable): a #GError, or %NULL
 *
 * Collect what the user chose.
 *
 * Returns: %TRUE if the prompt was confirmed, %FALSE if it was dismissed, in
 *          which case the out parameters are left untouched
 */
G_PASTE_VISIBLE gboolean
g_paste_prompt_migration_finish (GPastePrompt   *self,
                                 GAsyncResult   *result,
                                 GPasteStorage  *chosen,
                                 gboolean       *import,
                                 gboolean       *cleanup,
                                 GError        **error)
{
    g_return_val_if_fail (_G_PASTE_IS_PROMPT (self), FALSE);
    g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
    g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) == g_paste_prompt_migration_async, FALSE);

    g_autofree MigrationReply *reply = g_task_propagate_pointer (G_TASK (result), error);

    if (!reply)
        return FALSE;

    if (chosen)
        *chosen = reply->chosen;
    if (import)
        *import = reply->import;
    if (cleanup)
        *cleanup = reply->cleanup;

    return TRUE;
}

/**
 * g_paste_prompt_list_storage_backends:
 * @n_offered: (out): the number of backends returned
 *
 * List the storage backends a migration prompt should offer, in display order:
 * whichever flavors this build can actually construct.
 *
 * Returns: (array length=n_offered) (transfer none): the backends
 */
G_PASTE_VISIBLE const GPasteStorage *
g_paste_prompt_list_storage_backends (guint *n_offered)
{
    static const GPasteStorage offered[] = {
        G_PASTE_STORAGE_FILE,
#ifdef G_PASTE_ENABLE_ENCRYPTION
        G_PASTE_STORAGE_ENCRYPTED_FILE,
#endif
#ifdef G_PASTE_ENABLE_SQLITE
        G_PASTE_STORAGE_SQLITE,
#ifdef G_PASTE_ENABLE_ENCRYPTION
        G_PASTE_STORAGE_ENCRYPTED_SQLITE,
#endif
#endif
        G_PASTE_STORAGE_NOOP,
    };

    g_return_val_if_fail (n_offered, NULL);

    *n_offered = G_N_ELEMENTS (offered);

    return offered;
}

/**
 * g_paste_prompt_storage_label:
 * @storage_kind: a #GPasteStorage
 *
 * Get the translated label describing @storage_kind to the user.
 *
 * Returns: read-only translated string
 */
G_PASTE_VISIBLE const gchar *
g_paste_prompt_storage_label (GPasteStorage storage_kind)
{
    switch (storage_kind)
    {
    case G_PASTE_STORAGE_FILE:
        return _("Store the history in a file");
    case G_PASTE_STORAGE_ENCRYPTED_FILE:
        return _("Store the history in an encrypted file");
    case G_PASTE_STORAGE_SQLITE:
        return _("Store the history in a database");
    case G_PASTE_STORAGE_ENCRYPTED_SQLITE:
        return _("Store the history in an encrypted database");
    case G_PASTE_STORAGE_NOOP:
        return _("Don't store anything");
    default:
        return "";
    }
}

/**
 * g_paste_prompt_can_import:
 * @current: the backend the history currently lives in
 * @chosen: the backend the user picked
 *
 * Whether importing makes sense: only when moving from a backend that has
 * stored data into a different one that also stores something. Copying
 * file -> file, importing into "no storage", or importing from an empty "no
 * storage" source are all pointless.
 *
 * Returns: whether the "import existing data" toggle should be offered
 */
G_PASTE_VISIBLE gboolean
g_paste_prompt_can_import (GPasteStorage current,
                           GPasteStorage chosen)
{
    return current != G_PASTE_STORAGE_NOOP &&
           chosen != G_PASTE_STORAGE_NOOP &&
           chosen != current;
}

/**
 * g_paste_prompt_backend_changes:
 * @current: the backend the history currently lives in
 * @chosen: the backend the user picked
 *
 * Whether there is "old data" to delete afterwards: only once we actually leave
 * a backend that stored something, otherwise deleting it would throw away what
 * we kept.
 *
 * Returns: whether the "delete old data" toggle should be offered
 */
G_PASTE_VISIBLE gboolean
g_paste_prompt_backend_changes (GPasteStorage current,
                                GPasteStorage chosen)
{
    return current != G_PASTE_STORAGE_NOOP && chosen != current;
}

/**
 * g_paste_prompt_keyring_available:
 *
 * Whether this build can remember the passphrase at all, i.e. whether it was
 * built with libsecret. A prompt with no keyring behind it must not offer to
 * remember anything.
 *
 * Returns: whether there is a keyring to remember passphrases in
 */
G_PASTE_VISIBLE gboolean
g_paste_prompt_keyring_available (void)
{
#ifdef G_PASTE_ENABLE_LIBSECRET
    return TRUE;
#else
    return FALSE;
#endif
}

/**
 * g_paste_prompt_keyring_has_passphrase:
 *
 * Whether a passphrase is currently remembered, without installing it. Says
 * nothing about whether it still decrypts anything — that is what makes it the
 * right question for "is this user opted in to remembering?", a stale entry
 * being precisely one that wants replacing.
 *
 * Returns: whether a passphrase is remembered
 */
G_PASTE_VISIBLE gboolean
g_paste_prompt_keyring_has_passphrase (void)
{
#ifdef G_PASTE_ENABLE_LIBSECRET
    return g_paste_storage_keyring_has_passphrase ();
#else
    return FALSE;
#endif
}

/**
 * g_paste_prompt_remember_state:
 * @requested: what the request carries, i.e. the choice made on a previous
 *             attempt, or %UNCHANGED when there has not been one
 * @starts_on: (out): whether the switch should start on
 * @can_forget: (out): whether an off switch is a request to forget
 *
 * Work out how to present the "Remember this passphrase" switch.
 *
 * @starts_on carries forward a choice the user already made, so a re-prompt
 * after a wrong passphrase does not silently undo it; with no such choice it
 * follows the keyring, so someone who had their passphrase remembered stays
 * opted in and changing it replaces the stored one instead of leaving a stale
 * entry behind.
 *
 * @can_forget is deliberately a different question. An off switch is a request
 * to forget only when there is something remembered to forget — deriving that
 * from where the switch started would lose an explicit "forget" made on a
 * previous, mistyped attempt, since the retry starts the switch off and
 * off-because-you-turned-it-off would read as off-because-there-was-nothing.
 * A keyring we could not read is never taken as permission to delete.
 */
G_PASTE_VISIBLE void
g_paste_prompt_remember_state (GPasteStorageRemember requested,
                               gboolean             *starts_on,
                               gboolean             *can_forget)
{
    g_return_if_fail (starts_on);
    g_return_if_fail (can_forget);

    /* One lookup: it is a blocking round trip to the secret service, and it
     * answers both questions. */
    gboolean remembered = g_paste_prompt_keyring_has_passphrase ();

    *starts_on = (requested == G_PASTE_STORAGE_REMEMBER_UNCHANGED)
               ? remembered
               : (requested == G_PASTE_STORAGE_REMEMBER_YES);
    *can_forget = remembered;
}

/**
 * g_paste_prompt_passphrase_is_complete:
 * @passphrase: (nullable): what was typed in the passphrase field
 * @confirmation: (nullable): what was typed in the confirmation field, or %NULL
 *                when the prompt is not asking for one
 *
 * Whether the prompt would accept this as an answer.
 *
 * An empty passphrase is no passphrase, and when a confirmation is being asked
 * for the two have to match — the same rule the reply normalises against, so a
 * prompt never enables its accept button for something that comes back %NULL.
 *
 * Returns: whether the prompt can be accepted
 */
G_PASTE_VISIBLE gboolean
g_paste_prompt_passphrase_is_complete (const gchar *passphrase,
                                       const gchar *confirmation)
{
    if (!passphrase || !*passphrase)
        return FALSE;

    return !confirmation || g_paste_str_equal (passphrase, confirmation);
}

/**
 * g_paste_prompt_pwquality_available:
 *
 * Whether this build can rate a passphrase, i.e. whether it was built with
 * libpwquality. A prompt that cannot must say so rather than show a meter
 * pinned at zero.
 *
 * Returns: whether g_paste_prompt_passphrase_strength() can rate anything
 */
G_PASTE_VISIBLE gboolean
g_paste_prompt_pwquality_available (void)
{
#ifdef G_PASTE_ENABLE_PWQUALITY
    return TRUE;
#else
    return FALSE;
#endif
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
#endif

/**
 * g_paste_prompt_passphrase_strength:
 * @passphrase: (nullable): the passphrase to rate
 * @hint: (out) (transfer full) (nullable): the rating word, or libpwquality's
 *        own advice on a hard failure
 *
 * GNOME-style passphrase rating via libpwquality (as gnome-control-center
 * does): map the 0-100 score to a 0-4 meter level and produce an actionable
 * hint. On a hard failure (too short, dictionary word, ...) libpwquality
 * returns a negative code whose localized reason becomes the hint.
 *
 * Built without libpwquality there is no rating to give: the level is 0 and
 * @hint is %NULL. A prompt should say so rather than leave the rating out —
 * someone choosing a passphrase should know it is not being judged, instead of
 * reading a silent absence as approval.
 *
 * Returns: the meter level, from 0 (nothing to say) to 4 (strong)
 */
G_PASTE_VISIBLE guint
g_paste_prompt_passphrase_strength (const gchar  *passphrase,
                                    gchar       **hint)
{
    g_return_val_if_fail (hint, 0);

    *hint = NULL;

#ifdef G_PASTE_ENABLE_PWQUALITY
    if (!passphrase || !*passphrase)
        return 0;

    /* Re-reading the config on every keystroke would be wasteful, and it cannot
     * change under us within a process. */
    static pwquality_settings_t *pwq = NULL;

    if (!pwq)
    {
        pwq = pwquality_default_settings ();

        /* Out of memory. Degrade to "no rating" rather than dereferencing it —
         * and note the guard re-runs next keystroke, so this retries rather
         * than latching. */
        if (!pwq)
            return 0;

        pwquality_read_config (pwq, NULL, NULL);
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
                :                G_PASTE_PROMPT_STRENGTH_MAX;

    *hint = g_strdup (passphrase_rating (level));

    return level;
#else
    (void) passphrase;

    return 0;
#endif
}
