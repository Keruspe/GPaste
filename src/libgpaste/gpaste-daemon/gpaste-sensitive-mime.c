// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <string.h>

#include <gpaste-daemon/gpaste-sensitive-mime.h>

static const gchar *sensitive_mimes[G_PASTE_SENSITIVE_MIME_LAST] = {
    [G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT] = "x-kde-passwordManagerHint",
};

/* What a mime type may be said under. The first is the one we say ourselves; the
 * rest are spellings we accept from others, so a second one costs a line. */
static const gchar * const kde_password_manager_hint_values[] = { "secret", NULL };

static const gchar * const *sensitive_values[G_PASTE_SENSITIVE_MIME_LAST] = {
    [G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT] = kde_password_manager_hint_values,
};

/**
 * g_paste_sensitive_mime_get:
 * @mime: the hint we want the mime type of
 *
 * Find the MIME type a clipboard owner says a secret under
 *
 * Returns: the MIME type string corresponding to @mime
 */
G_PASTE_VISIBLE const gchar *
g_paste_sensitive_mime_get (GPasteSensitiveMime mime)
{
    g_return_val_if_fail (mime < G_PASTE_SENSITIVE_MIME_LAST, NULL);

    return sensitive_mimes[mime];
}

/**
 * g_paste_sensitive_mime_get_bytes:
 * @mime: the hint we want the value of
 *
 * Get what to publish under @mime to say that what we are offering is a secret:
 * the first of the values @mime is recognised by.
 *
 * Built once and kept, the values being literals. Under g_once_init_enter ()
 * so concurrent clipboard readers share the same immutable values.
 *
 * Returns: (transfer none): the #GBytes to offer under @mime
 */
G_PASTE_VISIBLE GBytes *
g_paste_sensitive_mime_get_bytes (GPasteSensitiveMime mime)
{
    static GBytes *bytes[G_PASTE_SENSITIVE_MIME_LAST] = { NULL };
    static gsize   initialized = 0;

    g_return_val_if_fail (mime < G_PASTE_SENSITIVE_MIME_LAST, NULL);

    if (g_once_init_enter (&initialized))
    {
        for (GPasteSensitiveMime m = G_PASTE_SENSITIVE_MIME_FIRST; m < G_PASTE_SENSITIVE_MIME_LAST; ++m)
        {
            const gchar *value = sensitive_values[m][0];

            bytes[m] = g_bytes_new_static (value, strlen (value));
        }

        g_once_init_leave (&initialized, 1);
    }

    return bytes[mime];
}

/**
 * g_paste_sensitive_mime_matches:
 * @mime: the hint @bytes were read under
 * @bytes: (nullable): what the clipboard owner offered under it
 *
 * Whether @bytes are one of the values @mime says a secret under. A mime type
 * being offered is not enough on its own: the same key is how an owner says the
 * content is *not* one.
 *
 * Returns: whether @bytes say the content is a secret
 */
G_PASTE_VISIBLE gboolean
g_paste_sensitive_mime_matches (GPasteSensitiveMime mime,
                                GBytes             *bytes)
{
    g_return_val_if_fail (mime < G_PASTE_SENSITIVE_MIME_LAST, FALSE);

    if (!bytes)
        return FALSE;

    gsize size;
    const gchar *data = g_bytes_get_data (bytes, &size);

    if (!data)
        return FALSE;

    /* A selection hands over bytes, not a string: an owner is free to terminate
     * the value or to end it with a newline, and both are the same answer. */
    while (size && (!data[size - 1] || g_ascii_isspace (data[size - 1])))
        --size;

    for (const gchar * const *value = sensitive_values[mime]; *value; ++value)
    {
        if (strlen (*value) == size && !memcmp (*value, data, size))
            return TRUE;
    }

    return FALSE;
}
