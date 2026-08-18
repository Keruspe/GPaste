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
void   g_paste_file_backend_delete_image   (const gchar *path);

#ifdef G_PASTE_ENABLE_ENCRYPTION
GPasteStorageBackend *g_paste_file_backend_new_encrypted (GPasteSettings *settings,
                                                          const gchar    *passphrase);

#endif

G_END_DECLS
