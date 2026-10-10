// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-storage.h>

G_PASTE_VISIBLE G_DEFINE_ENUM_TYPE (GPasteStorage, g_paste_storage,
                                    G_DEFINE_ENUM_VALUE (G_PASTE_STORAGE_NOOP,             "None"),
                                    G_DEFINE_ENUM_VALUE (G_PASTE_STORAGE_FILE,             "File"),
                                    G_DEFINE_ENUM_VALUE (G_PASTE_STORAGE_ENCRYPTED_FILE,   "EncryptedFile"),
                                    G_DEFINE_ENUM_VALUE (G_PASTE_STORAGE_SQLITE,           "Sqlite"),
                                    G_DEFINE_ENUM_VALUE (G_PASTE_STORAGE_ENCRYPTED_SQLITE, "EncryptedSqlite"))

/**
 * g_paste_storage_is_encrypted:
 * @storage_kind: a #GPasteStorage kind
 *
 * Whether @storage_kind encrypts the history on disk. This classifies the kind
 * itself, independently of the features built in: a build unable to construct
 * an encrypted backend must degrade it to "no storage", never to plaintext.
 *
 * Returns: %TRUE for the encrypted storage kinds
 */
G_PASTE_VISIBLE gboolean
g_paste_storage_is_encrypted (GPasteStorage storage_kind)
{
    return storage_kind == G_PASTE_STORAGE_ENCRYPTED_FILE ||
           storage_kind == G_PASTE_STORAGE_ENCRYPTED_SQLITE;
}
