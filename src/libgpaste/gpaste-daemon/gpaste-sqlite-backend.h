// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-storage-backend.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_SQLITE_BACKEND (g_paste_sqlite_backend_get_type ())

G_PASTE_FINAL_TYPE (SqliteBackend, sqlite_backend, SQLITE_BACKEND, GPasteStorageBackend)

#ifdef G_PASTE_ENABLE_ENCRYPTION
GPasteStorageBackend *g_paste_sqlite_backend_new_encrypted (GPasteSettings *settings,
                                                            const gchar    *passphrase);

gboolean g_paste_sqlite_backend_passphrase_can_decrypt (GPasteSettings *settings,
                                                        const gchar    *passphrase);
#endif

G_END_DECLS
