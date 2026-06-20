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
    GOutputStream *(*get_output_stream) (const GPasteFileBackend *self,
                                         GFile                   *output_file);
};

#ifdef G_PASTE_ENABLE_ENCRYPTION
GPasteStorageBackend *g_paste_file_backend_new_encrypted (GPasteSettings *settings,
                                                          const gchar    *passphrase);

gboolean g_paste_file_backend_passphrase_can_decrypt (GPasteSettings *settings,
                                                      const gchar    *passphrase);
#endif

G_END_DECLS
