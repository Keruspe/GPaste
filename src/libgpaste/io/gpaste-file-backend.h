/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_FILE_BACKEND_H__
#define __G_PASTE_FILE_BACKEND_H__

#include <gpaste-storage-backend.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_FILE_BACKEND (g_paste_file_backend_get_type ())

G_PASTE_FINAL_TYPE (FileBackend, file_backend, FILE_BACKEND, GPasteStorageBackend)

G_END_DECLS

#endif /*__G_PASTE_FILE_BACKEND_H__*/
