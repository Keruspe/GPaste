/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_STORAGE_BACKEND_H__
#define __G_PASTE_STORAGE_BACKEND_H__

#include <gpaste-macros.h>

G_BEGIN_DECLS

typedef enum {
    G_PASTE_STORAGE_FILE,
    G_PASTE_STORAGE_DEFAULT = G_PASTE_STORAGE_FILE
} GPasteStorage;

#define G_PASTE_TYPE_STORAGE_BACKEND (g_paste_storage_backend_get_type ())

G_PASTE_DERIVABLE_TYPE (StorageBackend, storage_backend, STORAGE_BACKEND, GObject)

struct _GPasteStorageBackendClass
{
    GObjectClass parent_class;

    /*< pure virtual >*/
    GList *(*read_history)  (const GPasteStorageBackend *self,
                             const gchar                *source);
    void   (*write_history) (const GPasteStorageBackend *self,
                             const gchar                *source,
                             const GList                *history);
};

GList *g_paste_storage_backend_read_history  (const GPasteStorageBackend *self);
void   g_paste_storage_backend_write_history (const GPasteStorageBackend *self,
                                              const GList                *history);

GPasteStorageBackend *g_paste_storage_backend_new (GPasteStorage storage_kind,
                                                   const gchar  *source);

G_END_DECLS

#endif /*__G_PASTE_STORAGE_BACKEND_H__*/
