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

/* Whether a passphrase is currently remembered, without installing it. Says
 * nothing about whether it still decrypts anything — that is what makes it the
 * right question for "is this user opted in to remembering?", a stale entry
 * being precisely one that wants replacing. */
gboolean g_paste_storage_keyring_has_passphrase (void);

/* Remember @passphrase in the keyring for future startups. Only ever called with
 * one already known to unlock the history: a passphrase that does not is worse
 * than none, since it costs a prompt every startup until it is discarded. */
void g_paste_storage_keyring_store (const gchar *passphrase);

/* Stop remembering the passphrase. Removing one that is not there is not an
 * error, so callers never have to look first. */
void g_paste_storage_keyring_clear (void);

G_END_DECLS
