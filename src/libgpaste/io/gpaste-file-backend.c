/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-file-backend.h>

struct _GPasteFileBackend
{
    GPasteStorageBackend parent_instance;
};

G_PASTE_DEFINE_TYPE (FileBackend, file_backend, G_PASTE_TYPE_STORAGE_BACKEND)

static GList *
g_paste_file_backend_read_history (const GPasteStorageBackend *self,
                                   const gchar                *source)
{
    /* TODO */
    return NULL;
}

static void
g_paste_file_backend_write_history (const GPasteStorageBackend *self,
                                    const gchar                *source,
                                    const GList                *history)
{
    /* TODO */
}

static void
g_paste_file_backend_class_init (GPasteFileBackendClass *klass)
{
    GPasteStorageBackendClass *storage_class = G_PASTE_STORAGE_BACKEND_CLASS (klass);

    storage_class->read_history = g_paste_file_backend_read_history;
    storage_class->write_history = g_paste_file_backend_write_history;
}

static void
g_paste_file_backend_init (GPasteFileBackend *self G_GNUC_UNUSED)
{
}
