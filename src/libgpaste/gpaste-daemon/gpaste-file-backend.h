// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-storage-backend.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_FILE_BACKEND (g_paste_file_backend_get_type ())

G_PASTE_DERIVABLE_TYPE (FileBackend, file_backend, FILE_BACKEND, GPasteStorageBackend)

struct _GPasteFileBackendClass
{
    GPasteStorageBackendClass parent_class;

    /*< protected >*/
    GOutputStream *(*get_output_stream) (GPasteFileBackend *self,
                                         GFile             *output_file);
};

/* The images/<history_name>/<checksum>.png layout, which is this backend's
 * alone: it is the one that materializes an image's bytes beside its store
 * rather than inside it. Per history, so the same image copied in several
 * histories gets a file each and evicting it from one never breaks the others.
 * An item is told the path it was loaded from and nothing more -- a backend
 * that keeps its images in its own store tells it nothing. */
gchar *g_paste_file_backend_images_dir     (const gchar *history_name);
gchar *g_paste_file_backend_image_path     (const gchar *history_name,
                                            const gchar *checksum);
gchar *g_paste_file_backend_encrypted_path (const gchar *path);

void g_paste_file_backend_sweep_legacy_images (void);

/* What a re-key puts beside each file it changes: the file's replacement under
 * the new key, and the original under a second name while the replacements go
 * in. Both are named from the file alone, and the second, beside the history's
 * own, is the longest file an encrypted history
 * needs (g_paste_storage_name_fits ()). */
#define G_PASTE_FILE_BACKEND_REKEY_NEW ".rekey"
#define G_PASTE_FILE_BACKEND_REKEY_OLD ".rekey-old"

#ifdef G_PASTE_ENABLE_ENCRYPTION
GPasteStorageBackend *g_paste_file_backend_new_encrypted (GPasteSettings *settings,
                                                          const gchar    *passphrase);

void g_paste_file_backend_settle_rekeys (void);
#endif

G_END_DECLS
