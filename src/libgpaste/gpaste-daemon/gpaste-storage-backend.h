// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-settings.h>
#include <gpaste-3/gpaste-storage.h>

#include <gpaste-daemon/gpaste-item.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_STORAGE_BACKEND (g_paste_storage_backend_get_type ())

G_PASTE_DERIVABLE_TYPE (StorageBackend, storage_backend, STORAGE_BACKEND, GObject)

struct _GPasteStorageBackendClass
{
    GObjectClass parent_class;

    /*< pure virtual >*/
    /* Every vfunc here is given the history's *name*, not a path: the path is
     * the backend's own, derived with g_paste_storage_backend_get_history_file_path().
     *
     * Returns %FALSE when the history file is present but could not be read back
     * (a failed decryption, parse or I/O error), so a caller can tell a genuine
     * empty history apart from a read that silently yielded nothing. */
    gboolean (*read_history_file)  (GPasteStorageBackend *self,
                                    const gchar          *name,
                                    GList                **history,
                                    gsize                *size);
    void (*write_history_file) (GPasteStorageBackend *self,
                                const gchar          *name,
                                const GList          *history);

    /*< protected >*/
    /* Which flavour this instance is. Everything else about the flavour --
     * the on-disk extension, whether it encrypts, whether it may keep password
     * items -- follows from it, so this is the one thing a backend answers. */
    GPasteStorage         (*get_kind)       (GPasteStorageBackend *self);
    void                  (*delete_history) (GPasteStorageBackend *self,
                                             const gchar          *name,
                                             GError               **error);
    GStrv                 (*list_histories) (GPasteStorageBackend *self,
                                             GError               **error);

    /*< protected, optional: how many items a history holds >*/
    /* Answer without materializing the history, where the store can count on its
     * own. The default reads the history and counts what came back, which is
     * what listing every history's size costs when a backend has nothing
     * cheaper; a store that can count must answer the same set read_history_file
     * would have returned, cap and favourites included, or a listing and the
     * history it leads to would disagree. */
    gsize    (*count_history)              (GPasteStorageBackend *self,
                                            const gchar          *name);

    /*< protected, optional: passphrase verification >*/
    /* Whether the history called @name proves this backend's passphrase wrong:
     * it holds encrypted data the backend cannot open. Everything else -- an
     * empty placeholder, a truncated or foreign file, an I/O error -- is
     * inconclusive and must answer %FALSE: condemning a passphrase over a
     * corrupt file tells the user their correct one is wrong.
     *
     * Only ever asked of a backend built with the passphrase under test. */
    gboolean (*history_refutes_passphrase) (GPasteStorageBackend *self,
                                            const gchar          *name);

    /*< protected, optional: re-encrypt an existing history under a new key >*/
    /* @self holds the passphrase the history is currently encrypted with; only
     * the encrypted flavors implement this. Returns %FALSE when @name was left
     * as it was, so a caller re-keying several histories can stop instead of
     * ending up with a set split across two passphrases. */
    gboolean (*rekey)                (GPasteStorageBackend *self,
                                      const gchar          *name,
                                      const gchar          *new_passphrase);

    /*< protected, optional: data materialized outside the store >*/
    /* Drop whatever this backend wrote for @item beyond the history itself --
     * an image's cache file, and so far nothing else. Only a backend that puts
     * an item's data beside its store rather than inside it has any: a database
     * blob goes when its row does. Called once the history has dropped the
     * item for good, so a backend keeping everything in one place implements
     * nothing here. */
    void     (*drop_item_data)       (GPasteStorageBackend *self,
                                      const gchar          *name,
                                      GPasteItem           *item);

    /*< protected, optional: incremental updates >*/
    /* @history is the whole history as it now stands, for reconciling whatever
     * rode along with the add -- a dedup, a grown line, an eviction. It is
     * %NULL when nothing did, which is the common case: reconciliation only
     * ever drops rows the history no longer has, so there is then nothing to
     * do. A backend that does not implement all four of these gets the full
     * history written out instead, and so always sees a non-%NULL one. */
    void     (*add_item)             (GPasteStorageBackend *self,
                                      const gchar          *name,
                                      GPasteItem           *item,
                                      const GList          *history);
    void     (*remove_item)          (GPasteStorageBackend *self,
                                      const gchar          *name,
                                      const gchar          *uuid);
    void     (*replace_item)         (GPasteStorageBackend *self,
                                      const gchar          *name,
                                      const gchar          *old_uuid,
                                      GPasteItem           *item);
    void     (*clear_history)        (GPasteStorageBackend *self,
                                      const gchar          *name);
};

GPasteSettings *g_paste_storage_backend_get_settings (GPasteStorageBackend *self);

GPasteStorage  g_paste_storage_backend_get_kind      (GPasteStorageBackend *self);
const gchar   *g_paste_storage_backend_get_extension (GPasteStorageBackend *self);
gboolean       g_paste_storage_backend_is_encrypted  (GPasteStorageBackend *self);

gchar *g_paste_storage_backend_get_history_file_path (GPasteStorageBackend *self,
                                                      const gchar          *name);

gboolean g_paste_storage_backend_read_history (GPasteStorageBackend *self,
                                               const gchar          *name,
                                               GList               **history,
                                               gsize                *size);
void g_paste_storage_backend_write_history    (GPasteStorageBackend *self,
                                               const gchar          *name,
                                               const GList          *history);
void g_paste_storage_backend_delete_history   (GPasteStorageBackend *self,
                                               const gchar          *name,
                                               GError              **error);
GStrv g_paste_storage_backend_list_histories  (GPasteStorageBackend *self,
                                               GError              **error);
gsize g_paste_storage_backend_count_history   (GPasteStorageBackend *self,
                                               const gchar          *name);
gboolean g_paste_storage_backend_delete_history_images (const gchar *name,
                                                        GError     **error);
gboolean g_paste_storage_backend_rekey        (GPasteStorageBackend *self,
                                               const gchar          *name,
                                               const gchar          *new_passphrase);

void     g_paste_storage_backend_add_item             (GPasteStorageBackend *self,
                                                       const gchar          *name,
                                                       GPasteItem           *item,
                                                       const GList          *history);
void     g_paste_storage_backend_remove_item          (GPasteStorageBackend *self,
                                                       const gchar          *name,
                                                       const gchar          *uuid,
                                                       const GList          *history);
void     g_paste_storage_backend_replace_item         (GPasteStorageBackend *self,
                                                       const gchar          *name,
                                                       const gchar          *old_uuid,
                                                       GPasteItem           *item,
                                                       const GList          *history);
void     g_paste_storage_backend_clear_history        (GPasteStorageBackend *self,
                                                       const gchar          *name,
                                                       const GList          *history);
void     g_paste_storage_backend_drop_item_data       (GPasteStorageBackend *self,
                                                       const gchar          *name,
                                                       GPasteItem           *item);

gboolean g_paste_storage_backend_is_incremental       (GPasteStorageBackend *self);

void g_paste_storage_backend_lock   (void);
void g_paste_storage_backend_unlock (void);

GPasteStorageBackend *g_paste_storage_backend_new (GPasteStorage   storage_kind,
                                                   GPasteSettings *settings);
GPasteStorageBackend *g_paste_storage_backend_new_with_passphrase (GPasteStorage   storage_kind,
                                                                   GPasteSettings *settings,
                                                                   const gchar    *passphrase);

const gchar *g_paste_storage_get_extension (GPasteStorage storage_kind);

#ifdef G_PASTE_ENABLE_ENCRYPTION
void         g_paste_storage_backend_set_passphrase (const gchar *passphrase);
const gchar *g_paste_storage_backend_get_passphrase (void);

gboolean g_paste_storage_passphrase_can_decrypt (GPasteStorage   storage_kind,
                                                 GPasteSettings *settings,
                                                 const gchar    *passphrase);
#endif

G_END_DECLS
