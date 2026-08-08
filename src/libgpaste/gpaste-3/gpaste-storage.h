// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gpaste-3/gpaste-macros.h>

G_BEGIN_DECLS

/* The flavors the history can be persisted in: the values of the
 * "storage-backend" setting (g_paste_settings_get_storage_backend()). Building
 * the matching backend is the daemon library's job
 * (g_paste_storage_backend_new()); the kind itself lives here so everything
 * reading the setting — the preferences UI included, which sits below the daemon
 * library — can tell the flavors apart. */
typedef enum {
    G_PASTE_STORAGE_NOOP,
    G_PASTE_STORAGE_FILE,
    G_PASTE_STORAGE_ENCRYPTED_FILE,
    G_PASTE_STORAGE_SQLITE,
    G_PASTE_STORAGE_ENCRYPTED_SQLITE,
    G_PASTE_N_STORAGE, /* must stay last, before the aliases */
    G_PASTE_STORAGE_DEFAULT = G_PASTE_STORAGE_FILE
} GPasteStorage;

gboolean g_paste_storage_is_encrypted (GPasteStorage storage_kind);

G_END_DECLS
