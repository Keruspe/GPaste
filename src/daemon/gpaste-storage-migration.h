// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste/gpaste-settings.h>

#include <adwaita.h>

G_BEGIN_DECLS

/* Bump whenever the storage layout changes in a way that should prompt the user
 * to (re)choose a backend. The dialog is shown on startup while the stored
 * "storage-backend-revision" differs from this. */
#define G_PASTE_STORAGE_BACKEND_REVISION 1

typedef void (*GPasteStorageMigrationDoneFunc) (gpointer user_data);

gboolean g_paste_storage_migration_needed (GPasteSettings *settings);

void g_paste_storage_migration_show (GtkApplication                 *application,
                                     GPasteSettings                 *settings,
                                     GPasteStorageMigrationDoneFunc  done,
                                     gpointer                        user_data);

void g_paste_storage_migration_register_action (GtkApplication *application,
                                                GPasteSettings *settings);

G_END_DECLS
