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

/* Whether @path names something other than a regular file -- a FIFO, a device,
 * a directory --, symbolic links followed: asked before a store file is
 * opened, which for a FIFO holds open(2) until a writer comes, the main loop's
 * reads -- a listing's counts, the unlock's probes -- included. A file swapped
 * in after the check still blocks; the history directory being private is
 * what keeps anyone else from planting one at all. */
gboolean g_paste_storage_is_special_file (const gchar *path);

/* Whether deleting or emptying @name through @self reaches every store keeping
 * it. The "no storage" backend reaches none: a flavour keeping @name is an
 * encrypted store left locked, which a dismissed unlock has the daemon run on
 * that backend over, or what a migration to it kept. Nor does any backend reach
 * a flavour this build cannot open, which the file flavour stands in for
 * (storage_flavours). A deletion answered for with such a store still on disk
 * would have the history believed gone, back once that store is opened. */
gboolean g_paste_storage_backend_reaches_history (GPasteStorageBackend *self,
                                                  const gchar          *name);

/* How many times this process has let go of the storage lock
 * (g_paste_storage_backend_unlock ()), and g_paste_storage_backend_lock () from
 * that count on: it ends short of the lock should this process have let go of
 * it since the count was read. For a wait decided on one thread and made on
 * another, which a release in between has to end as it ends a wait already
 * going. */
guint    g_paste_storage_backend_get_releases (void);
gboolean g_paste_storage_backend_lock_since   (guint         releases,
                                               GCancellable *cancellable);

G_END_DECLS
