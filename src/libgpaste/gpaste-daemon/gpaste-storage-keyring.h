// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* Look the encryption passphrase up in the keyring and, if found, install it as
 * the storage backend passphrase. Returns whether one was found. */
gboolean g_paste_storage_keyring_apply (void);

/* Remember @passphrase in the keyring for future startups. */
void g_paste_storage_keyring_store (const gchar *passphrase);

G_END_DECLS
