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

/* The remembered passphrase, or NULL when there is none. Free it with
 * secret_password_free(). */
static gchar *
g_paste_storage_keyring_lookup (void)
{
    g_autoptr (GError) error = NULL;
    gchar *passphrase = secret_password_lookup_sync (g_paste_storage_keyring_schema (), NULL, &error, NULL);

    if (error)
        g_warning ("Could not look up the history passphrase in the keyring: %s", error->message);

    return passphrase;
}

/* Internal on purpose: applying a remembered passphrase without checking that it
 * still decrypts the history is the footgun apply_verified() exists to close. */
static gboolean
g_paste_storage_keyring_apply (void)
{
    gchar *passphrase = g_paste_storage_keyring_lookup ();

    if (!passphrase)
        return FALSE;

    g_paste_storage_backend_set_passphrase (passphrase);
    secret_password_free (passphrase);

    return TRUE;
}

G_PASTE_VISIBLE gboolean
g_paste_storage_keyring_has_passphrase (void)
{
    g_autoptr (GError) error = NULL;
    /* Search rather than look up: without SECRET_SEARCH_LOAD_SECRETS this asks
     * whether the entry exists without fetching what is in it, so answering a
     * yes/no question never materializes the passphrase — and never makes the
     * user unlock their keyring just so a switch can pick its initial state. */
    g_autoptr (GHashTable) attributes = g_hash_table_new (g_str_hash, g_str_equal);
    g_autolist (SecretItem) items = secret_service_search_sync (NULL, /* default service */
                                                                g_paste_storage_keyring_schema (),
                                                                attributes,
                                                                SECRET_SEARCH_NONE,
                                                                NULL, /* cancellable */
                                                                &error);

    if (error)
        g_warning ("Could not look up the history passphrase in the keyring: %s", error->message);

    return items != NULL;
}

G_PASTE_VISIBLE gboolean
g_paste_storage_keyring_apply_verified (GPasteStorage   storage_kind,
                                        GPasteSettings *settings)
{
    if (!g_paste_storage_keyring_apply ())
        return FALSE;

    const gchar *passphrase = g_paste_storage_backend_get_passphrase ();

    if (passphrase && !g_paste_storage_passphrase_can_decrypt (storage_kind, settings, passphrase))
    {
        g_warning ("The passphrase stored in the keyring does not unlock the history");
        g_paste_storage_backend_set_passphrase (NULL);
        return FALSE;
    }

    return TRUE;
}

G_PASTE_VISIBLE void
g_paste_storage_keyring_store (const gchar *passphrase)
{
    g_return_if_fail (passphrase && *passphrase);

    g_autoptr (GError) error = NULL;

    if (!secret_password_store_sync (g_paste_storage_keyring_schema (), SECRET_COLLECTION_DEFAULT,
                                     "GPaste history passphrase", passphrase,
                                     NULL, &error, NULL) && error)
        g_warning ("Could not store the history passphrase in the keyring: %s", error->message);
}

G_PASTE_VISIBLE void
g_paste_storage_keyring_clear (void)
{
    g_autoptr (GError) error = NULL;

    /* Nothing to remove is not a failure (it returns %FALSE without setting the
     * error), so only a real one is worth reporting. */
    if (!secret_password_clear_sync (g_paste_storage_keyring_schema (), NULL, &error, NULL) && error)
        g_warning ("Could not remove the history passphrase from the keyring: %s", error->message);
}
