// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-settings.h>

#include <gpaste-daemon/gpaste-prompt.h>

G_BEGIN_DECLS

/* Bump whenever the storage layout changes in a way that should prompt the user
 * to (re)choose a backend. The dialog is shown on startup while the stored
 * "storage-backend-revision" differs from this. */
#define G_PASTE_STORAGE_BACKEND_REVISION 3

/* Whether the migration dialog should be shown: the stored backend revision
 * differs from G_PASTE_STORAGE_BACKEND_REVISION. */
gboolean g_paste_storage_migration_needed (GPasteSettings *settings);

/* Whether an encrypted history still needs decrypting before it can be loaded:
 * the backend is encrypted and no passphrase is set yet. When libsecret is built
 * this also applies a usable keyring passphrase as a side effect (returning
 * %FALSE then), so a %TRUE result means a prompt is genuinely required. */
gboolean g_paste_storage_decryption_needed (GPasteSettings *settings);

/* The three storage concerns are ordinary GAsyncResult operations: each _async
 * starts it and returns, and the matching _finish reads the outcome off the
 * #GAsyncResult. The caller drives the main loop (e.g. through
 * g_application_run()); none of these ever spins one of its own, and none builds
 * any UI either -- that is @prompt's business.
 *
 * They take no #GCancellable: a concern is a dialog the user is looking at, and
 * neither #GPastePrompt nor the portal underneath it can be told to take it back
 * down, so a cancellable here could only ever be ignored.
 *
 * _finish returns %FALSE when the prompt implementation itself failed -- the
 * built-in ones never do, an out-of-tree one (gnome-shell's) can -- and, for
 * the re-key alone, when the histories on disk could not be re-encrypted. Every
 * ordinary outcome, dismissal included, is %TRUE: what the user chose has
 * already been applied to the settings and to the process-wide passphrase by
 * the time the callback runs, so there is nothing else to hand back.
 *
 * A prompt that fails ends its concern rather than being asked again: a
 * dismissal is a question the user answered, an implementation error is not one
 * they can. */

/* Ask the user where GPaste should store the history, and act on the answer.
 * Always shows the prompt; deciding whether it is needed is the caller's job
 * (g_paste_storage_migration_needed()). */
void     g_paste_storage_migration_async  (GPastePrompt        *prompt,
                                           GPasteSettings      *settings,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data);
gboolean g_paste_storage_migration_finish (GAsyncResult        *result,
                                           GError             **error);

/* Unlock an already-encrypted history through a passphrase prompt. Only
 * meaningful after g_paste_storage_decryption_needed() returns %TRUE (which also
 * applies a usable keyring passphrase); completes immediately when there is
 * nothing to do. */
void     g_paste_storage_decryption_async  (GPastePrompt        *prompt,
                                            GPasteSettings      *settings,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data);
gboolean g_paste_storage_decryption_finish (GAsyncResult        *result,
                                            GError             **error);

/* Change the passphrase of the encrypted history: unlock it, ask for the
 * passphrase to replace it with (confirmed, and optionally remembered in the
 * keyring), then re-encrypt every history with it. The history never moves --
 * only its key changes. Completes immediately when the current backend is not
 * encrypted. The new passphrase is only adopted (and only remembered) once the
 * data on disk actually speaks it; a re-key that could not rewrite it fails, so
 * a passphrase change that did not happen is never reported as one that did. */
void     g_paste_storage_rekey_async  (GPastePrompt        *prompt,
                                       GPasteSettings      *settings,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data);
gboolean g_paste_storage_rekey_finish (GAsyncResult        *result,
                                       GError             **error);

G_END_DECLS
