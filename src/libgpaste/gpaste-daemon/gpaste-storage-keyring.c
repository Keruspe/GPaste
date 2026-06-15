// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-storage-keyring.h>
#include <gpaste-daemon/gpaste-storage-backend.h>

#include <libsecret/secret.h>

/* A single, attribute-less secret: the master passphrase of the encrypted
 * history. libsecret keeps it in non-pageable memory. */
static const SecretSchema *
g_paste_storage_keyring_schema (void)
{
    static const SecretSchema schema = {
        .name = "org.gnome.GPaste.StoragePassphrase",
        .flags = SECRET_SCHEMA_NONE,
        .attributes = {
            { "NULL", 0 },
        },
    };

    return &schema;
}

gboolean
g_paste_storage_keyring_apply (void)
{
    g_autoptr (GError) error = NULL;
    gchar *passphrase = secret_password_lookup_sync (g_paste_storage_keyring_schema (), NULL, &error, NULL);

    if (error)
        g_warning ("Could not look up the history passphrase in the keyring: %s", error->message);

    if (!passphrase)
        return FALSE;

    g_paste_storage_backend_set_passphrase (passphrase);
    secret_password_free (passphrase);

    return TRUE;
}

void
g_paste_storage_keyring_store (const gchar *passphrase)
{
    g_return_if_fail (passphrase && *passphrase);

    g_autoptr (GError) error = NULL;

    if (!secret_password_store_sync (g_paste_storage_keyring_schema (), SECRET_COLLECTION_DEFAULT,
                                     "GPaste history passphrase", passphrase,
                                     NULL, &error, NULL) && error)
        g_warning ("Could not store the history passphrase in the keyring: %s", error->message);
}
