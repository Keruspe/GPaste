// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-passphrase.h>

#define GCR_API_SUBJECT_TO_CHANGE
#include <gcr/gcr.h>

/* gcr is an unconditional dependency of this library, so a passphrase lives in
 * secure memory in every build — including one without the encryption feature,
 * which cannot use it for anything but can still be handed one to carry. */

G_DEFINE_BOXED_TYPE (GPastePassphrase, g_paste_passphrase,
                     g_paste_passphrase_copy, g_paste_passphrase_free)

/**
 * g_paste_passphrase_new:
 * @cleartext: (nullable): the passphrase to take a copy of
 *
 * Copy @cleartext into gcr secure (non-pageable) memory.
 *
 * An empty passphrase is no passphrase: it gives %NULL, so callers that only
 * null-check cannot end up configuring an unprotected "encrypted" history.
 *
 * Returns: (transfer full) (nullable): a new #GPastePassphrase, or %NULL
 */
G_PASTE_VISIBLE GPastePassphrase *
g_paste_passphrase_new (const gchar *cleartext)
{
    if (!cleartext || !*cleartext)
        return NULL;

    return (GPastePassphrase *) gcr_secure_memory_strdup (cleartext);
}

/**
 * g_paste_passphrase_copy:
 * @self: (nullable): a #GPastePassphrase
 *
 * Copy a passphrase, secure memory and all.
 *
 * Returns: (transfer full) (nullable): a new #GPastePassphrase, or %NULL
 */
G_PASTE_VISIBLE GPastePassphrase *
g_paste_passphrase_copy (const GPastePassphrase *self)
{
    return g_paste_passphrase_new ((const gchar *) self);
}

/**
 * g_paste_passphrase_free:
 * @self: (nullable) (transfer full): a #GPastePassphrase
 *
 * Wipe and release a passphrase.
 */
G_PASTE_VISIBLE void
g_paste_passphrase_free (GPastePassphrase *self)
{
    gcr_secure_memory_strfree ((gchar *) self);
}

/**
 * g_paste_passphrase_peek:
 * @self: (nullable): a #GPastePassphrase
 *
 * Get the cleartext, to hand to whatever needs the characters: deriving a key,
 * checking that it decrypts, storing it in the keyring.
 *
 * Returns: (nullable): read-only string owned by @self
 */
G_PASTE_VISIBLE const gchar *
g_paste_passphrase_peek (const GPastePassphrase *self)
{
    return (const gchar *) self;
}
