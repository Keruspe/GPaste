/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_STORAGE_BACKEND_H__
#define __G_PASTE_STORAGE_BACKEND_H__

#include <gpaste/gpaste-settings.h>

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
    void (*read_history_file)  (const GPasteStorageBackend *self,
                                const gchar                *history_file_path,
                                GList                     **history,
                                gsize                      *size);
    void (*write_history_file) (const GPasteStorageBackend *self,
                                const gchar                *history_file_path,
                                const GList                *history);

    /*< protected >*/
    const gchar          *(*get_extension) (const GPasteStorageBackend *self);
    const GPasteSettings *(*get_settings)  (const GPasteStorageBackend *self);
};

void g_paste_storage_backend_read_history  (const GPasteStorageBackend *self,
                                            const gchar                *name,
                                            GList                     **history,
                                            gsize                      *size);
void g_paste_storage_backend_write_history (const GPasteStorageBackend *self,
                                            const gchar                *name,
                                            const GList                *history);

GPasteStorageBackend *g_paste_storage_backend_new (GPasteStorage   storage_kind,
                                                   GPasteSettings *settings);

G_END_DECLS

#endif /*__G_PASTE_STORAGE_BACKEND_H__*/
