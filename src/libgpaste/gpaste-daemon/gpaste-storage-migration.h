// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste/gpaste-settings.h>

#include <adwaita.h>

G_BEGIN_DECLS

/* Bump whenever the storage layout changes in a way that should prompt the user
 * to (re)choose a backend. The dialog is shown on startup while the stored
 * "storage-backend-revision" differs from this. */
#define G_PASTE_STORAGE_BACKEND_REVISION 2

typedef void (*GPasteStorageMigrationDoneFunc) (gpointer user_data);

/* Receives the entered passphrase, or %NULL if the prompt was dismissed. */
typedef void (*GPasteStoragePassphraseFunc) (const gchar *passphrase,
                                             gpointer     user_data);

gboolean g_paste_storage_migration_needed (GPasteSettings *settings);

/* Synchronously get the history store ready before the daemon loads it: register
 * the migration action, run the migration dialog, and (when encryption is
 * enabled) unlock an encrypted history from the keyring or a passphrase prompt.
 * Spins a nested main loop while a dialog is up. When @force is %FALSE the dialog
 * is only shown if the stored revision changed (g_paste_storage_migration_needed);
 * when %TRUE it is always shown. Deciding whether a migration is needed is the
 * launcher's job (the standalone daemon, or daemon.js before spawning the helper),
 * so the helper executable always forces the dialog. */
void g_paste_storage_migration_prepare (GtkApplication *application,
                                        GPasteSettings *settings,
                                        gboolean        force);

/* Ask the user for the encrypted history passphrase. @confirm asks for it twice
 * (with a data-loss warning) when setting up a new encrypted history; otherwise
 * it is a single unlock field. Lives in the daemon, not the UI. */
void g_paste_storage_migration_prompt_passphrase (GtkApplication              *application,
                                                  gboolean                     confirm,
                                                  GPasteStoragePassphraseFunc  done,
                                                  gpointer                     user_data);

void g_paste_storage_migration_show (GtkApplication                 *application,
                                     GPasteSettings                 *settings,
                                     GPasteStorageMigrationDoneFunc  done,
                                     gpointer                        user_data);

void g_paste_storage_migration_register_action (GtkApplication *application,
                                                GPasteSettings *settings);

G_END_DECLS
