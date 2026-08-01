// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste/gpaste-settings.h>

#include <adwaita.h>

G_BEGIN_DECLS

/* Bump whenever the storage layout changes in a way that should prompt the user
 * to (re)choose a backend. The dialog is shown on startup while the stored
 * "storage-backend-revision" differs from this. */
#define G_PASTE_STORAGE_BACKEND_REVISION 3

typedef void (*GPasteStorageMigrationDoneFunc) (gpointer user_data);

/* What the prompt's "Remember this passphrase" switch says to do with the
 * keyring. UNCHANGED is not "no": it is the switch having been left as it was
 * found, which must not delete an entry — the switch starts off both when there
 * is nothing remembered and when the keyring could not be reached to ask. */
typedef enum {
    G_PASTE_STORAGE_REMEMBER_UNCHANGED,
    G_PASTE_STORAGE_REMEMBER_YES,
    G_PASTE_STORAGE_REMEMBER_NO
} GPasteStorageRemember;

/* Receives the entered passphrase, or %NULL if the prompt was dismissed, along
 * with what to do about remembering it. Acting on @remember is deliberately the
 * callback's job: only it can tell whether the passphrase turned out to be the
 * right one, and remembering a wrong one costs a prompt on every startup until
 * it is discarded. */
typedef void (*GPasteStoragePassphraseFunc) (const gchar          *passphrase,
                                             GPasteStorageRemember remember,
                                             gpointer              user_data);

/* Whether the migration dialog should be shown: the stored backend revision
 * differs from G_PASTE_STORAGE_BACKEND_REVISION. */
gboolean g_paste_storage_migration_needed (GPasteSettings *settings);

/* Whether an encrypted history still needs decrypting before it can be loaded:
 * the backend is encrypted and no passphrase is set yet. When libsecret is built
 * this also applies a usable keyring passphrase as a side effect (returning
 * %FALSE then), so a %TRUE result means a prompt is genuinely required. */
gboolean g_paste_storage_decryption_needed (GPasteSettings *settings);

/* Ask the user for the encrypted history passphrase. @confirm asks for it twice
 * (with a data-loss warning) when setting up a new encrypted history; otherwise
 * it is a single unlock field. @error_message, when set, is shown above the
 * entry (e.g. to re-prompt after a wrong passphrase). @remember is where the
 * "Remember this passphrase" switch starts: UNCHANGED asks the keyring, and
 * anything else carries a choice the user already made forward — which is what a
 * re-prompt after a wrong passphrase has to do, or it would silently undo it.
 * Lives in the daemon, not the UI. */
void g_paste_storage_migration_prompt_passphrase (GtkApplication              *application,
                                                  gboolean                     confirm,
                                                  const gchar                 *error_message,
                                                  GPasteStorageRemember        remember,
                                                  GPasteStoragePassphraseFunc  done,
                                                  gpointer                     user_data);

/* Show the migration dialog (the "migrate" concern) and call @done when it is
 * dismissed. Always shows it; deciding whether it is needed is the caller's job
 * (g_paste_storage_migration_needed()). The caller drives the main loop (e.g.
 * through g_application_run()); this never spins one of its own. */
void g_paste_storage_migration_show (GtkApplication                 *application,
                                     GPasteSettings                 *settings,
                                     GPasteStorageMigrationDoneFunc  done,
                                     gpointer                        user_data);

/* Unlock an already-encrypted history (the "decrypt" concern) through a
 * passphrase prompt, calling @done once settled. Only meaningful after
 * g_paste_storage_decryption_needed() returns %TRUE (which also applies a usable
 * keyring passphrase); @done is invoked immediately when there is nothing to do.
 * Like _show above, the caller owns the main loop. */
void g_paste_storage_decryption_show (GtkApplication                 *application,
                                      GPasteSettings                 *settings,
                                      GPasteStorageMigrationDoneFunc  done,
                                      gpointer                        user_data);

/* Change the passphrase of the encrypted history (the "re-key" concern):
 * unlock it, ask for the passphrase to replace it with (confirmed, and
 * optionally remembered in the keyring), then re-encrypt every history with it.
 * The history never moves — only its key changes. @done is invoked immediately
 * when the current backend is not encrypted. Like _show above, the caller owns
 * the main loop. */
void g_paste_storage_rekey_show (GtkApplication                 *application,
                                 GPasteSettings                 *settings,
                                 GPasteStorageMigrationDoneFunc  done,
                                 gpointer                        user_data);

G_END_DECLS
