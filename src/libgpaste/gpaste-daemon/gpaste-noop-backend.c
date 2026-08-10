// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-noop-backend.h>

struct _GPasteNoopBackend
{
    GPasteStorageBackend parent_instance;
};

G_PASTE_DEFINE_TYPE (NoopBackend, noop_backend, G_PASTE_TYPE_STORAGE_BACKEND)

/* The "no storage" backend persists nothing: the history lives only in memory
 * for the current session. Reads come back empty, writes are dropped, and no
 * history is ever listed on disk. */

static gboolean
g_paste_noop_backend_read_history_file (GPasteStorageBackend  *self G_GNUC_UNUSED,
                                        const gchar           *name G_GNUC_UNUSED,
                                        GList                **history,
                                        gsize                 *size)
{
    *history = NULL;
    *size = 0;

    /* Nothing to read is an authoritative empty history, not a failure. */
    return TRUE;
}

static void
g_paste_noop_backend_write_history_file (GPasteStorageBackend *self G_GNUC_UNUSED,
                                         const gchar          *name G_GNUC_UNUSED,
                                         const GList          *history G_GNUC_UNUSED)
{
}

static GPasteStorage
g_paste_noop_backend_get_kind (GPasteStorageBackend *self G_GNUC_UNUSED)
{
    return G_PASTE_STORAGE_NOOP;
}

static void
g_paste_noop_backend_class_init (GPasteNoopBackendClass *klass)
{
    GPasteStorageBackendClass *storage_class = G_PASTE_STORAGE_BACKEND_CLASS (klass);

    storage_class->read_history_file = g_paste_noop_backend_read_history_file;
    storage_class->write_history_file = g_paste_noop_backend_write_history_file;
    storage_class->get_kind = g_paste_noop_backend_get_kind;
}

static void
g_paste_noop_backend_init (GPasteNoopBackend *self G_GNUC_UNUSED)
{
}
