// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-storage.h>

G_PASTE_VISIBLE GType
g_paste_storage_get_type (void)
{
    static GType etype = 0;
    if (!etype)
    {
        static const GEnumValue values[] = {
            { G_PASTE_STORAGE_NOOP,             "G_PASTE_STORAGE_NOOP",             "None"            },
            { G_PASTE_STORAGE_FILE,             "G_PASTE_STORAGE_FILE",             "File"            },
            { G_PASTE_STORAGE_ENCRYPTED_FILE,   "G_PASTE_STORAGE_ENCRYPTED_FILE",   "EncryptedFile"   },
            { G_PASTE_STORAGE_SQLITE,           "G_PASTE_STORAGE_SQLITE",           "Sqlite"          },
            { G_PASTE_STORAGE_ENCRYPTED_SQLITE, "G_PASTE_STORAGE_ENCRYPTED_SQLITE", "EncryptedSqlite" },
            { 0,                                 NULL,                               NULL             }
        };
        etype = g_enum_register_static (g_intern_static_string ("GPasteStorage"), values);
        g_type_class_ref (etype);
    }
    return etype;
}

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
