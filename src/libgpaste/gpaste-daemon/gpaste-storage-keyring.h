// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-storage-backend.h>

G_BEGIN_DECLS

/* Look the encryption passphrase up in the keyring and install it as the storage
 * backend passphrase, but only when it actually decrypts @settings' history of
 * the @storage_kind flavor; a stale entry is discarded (so it can never be used
 * to overwrite the real data with an empty, wrongly-keyed one). Returns whether
 * a usable passphrase is now installed. Applying one unverified is deliberately
 * not exposed. */
gboolean g_paste_storage_keyring_apply_verified (GPasteStorage   storage_kind,
                                                 GPasteSettings *settings);

/* Remember @passphrase in the keyring for future startups. */
void g_paste_storage_keyring_store (const gchar *passphrase);

G_END_DECLS
