// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-storage-backend.h>

G_BEGIN_DECLS

/* Have @self read every item a history stores, past max-history-size and
 * whatever images-support says: a migration's import, which a cleanup may
 * follow, copies the store, not what a load would show of it -- as a change of
 * passphrase copies the files whole. */
void g_paste_storage_backend_read_whole (GPasteStorageBackend *self);

/* What a read of @self keeps: the non-favourite items up to this many, and
 * images or not. A count asks the same, to answer what a read would return. */
guint64  g_paste_storage_backend_get_read_cap (GPasteStorageBackend *self);
gboolean g_paste_storage_backend_reads_images (GPasteStorageBackend *self);

/* Delete the history called @name from the @storage_kind flavour, as its
 * backend's delete_history and the images that follow it would, with no
 * backend: no key is needed to unlink a file, and what takes back the copy an
 * import cut short left has none to give. */
void g_paste_storage_delete_history (GPasteStorage storage_kind,
                                     const gchar  *name,
                                     GError      **error);

G_END_DECLS
