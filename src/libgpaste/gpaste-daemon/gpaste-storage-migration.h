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

typedef void (*GPasteStorageMigrationDoneFunc) (gpointer user_data);

/* Whether the migration dialog should be shown: the stored backend revision
 * differs from G_PASTE_STORAGE_BACKEND_REVISION. */
gboolean g_paste_storage_migration_needed (GPasteSettings *settings);

/* Whether an encrypted history still needs decrypting before it can be loaded:
 * the backend is encrypted and no passphrase is set yet. When libsecret is built
 * this also applies a usable keyring passphrase as a side effect (returning
 * %FALSE then), so a %TRUE result means a prompt is genuinely required. */
gboolean g_paste_storage_decryption_needed (GPasteSettings *settings);

/* Show the migration prompt (the "migrate" concern) and call @done when it is
 * dismissed. Always shows it; deciding whether it is needed is the caller's job
 * (g_paste_storage_migration_needed()). The caller drives the main loop (e.g.
 * through g_application_run()); this never spins one of its own, and never
 * builds any UI either — that is @prompt's business. */
void g_paste_storage_migration_show (GPastePrompt                   *prompt,
                                     GPasteSettings                 *settings,
                                     GPasteStorageMigrationDoneFunc  done,
                                     gpointer                        user_data);

/* Unlock an already-encrypted history (the "decrypt" concern) through a
 * passphrase prompt, calling @done once settled. Only meaningful after
 * g_paste_storage_decryption_needed() returns %TRUE (which also applies a usable
 * keyring passphrase); @done is invoked immediately when there is nothing to do.
 * Like _show above, the caller owns the main loop. */
void g_paste_storage_decryption_show (GPastePrompt                   *prompt,
                                      GPasteSettings                 *settings,
                                      GPasteStorageMigrationDoneFunc  done,
                                      gpointer                        user_data);

/* Change the passphrase of the encrypted history (the "re-key" concern):
 * unlock it, ask for the passphrase to replace it with (confirmed, and
 * optionally remembered in the keyring), then re-encrypt every history with it.
 * The history never moves — only its key changes. @done is invoked immediately
 * when the current backend is not encrypted. Like _show above, the caller owns
 * the main loop. */
void g_paste_storage_rekey_show (GPastePrompt                   *prompt,
                                 GPasteSettings                 *settings,
                                 GPasteStorageMigrationDoneFunc  done,
                                 gpointer                        user_data);

G_END_DECLS
