// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-daemon-util.h>

#include <string.h>

/**
 * g_paste_util_replace:
 * @text: the initial text
 * @pattern: the pattern to replace
 * @substitution: the replacement text
 *
 * Replace some text
 *
 * Returns: the newly allocated string
 */
G_PASTE_VISIBLE gchar *
g_paste_util_replace (const gchar *text,
                      const gchar *pattern,
                      const gchar *substitution)
{
    g_return_val_if_fail (g_utf8_validate (text, -1, NULL), NULL);
    g_return_val_if_fail (g_utf8_validate (pattern, -1, NULL), NULL);
    g_return_val_if_fail (g_utf8_validate (substitution, -1, NULL), NULL);

    g_autofree gchar *regex_string = g_regex_escape_string (pattern, -1);
    g_autoptr (GError) error = NULL;
    g_autoptr (GRegex) regex = g_regex_new (regex_string,
                                            0, /* Compile options */
                                            0, /* Match options */
                                            &error);
    g_assert_no_error (error);
    gchar *result = g_regex_replace_literal (regex,
                                             text,
                                             (gssize) -1,
                                             0, /* Start position */
                                             substitution,
                                             0, /* Match options */
                                             &error);
    g_assert_no_error (error);
    return result;
}

/**
 * g_paste_util_xml_decode:
 * @text: The text to decode
 *
 * Decode the text to its original pre-xml form
 *
 * Returns: the decoded text
 */
G_PASTE_VISIBLE gchar *
g_paste_util_xml_decode (const gchar *text)
{
    g_return_val_if_fail (text, NULL);

    g_autofree gchar *_decoded_text = g_paste_util_replace (text, "&gt;", ">");

    return g_paste_util_replace (_decoded_text, "&amp;", "&");
}

/**
 * g_paste_util_xml_encode:
 * @text: The text to encode
 *
 * Encode the text into its xml form
 *
 * Returns: the encoded text
 */
G_PASTE_VISIBLE gchar *
g_paste_util_xml_encode (const gchar *text)
{
    g_return_val_if_fail (text, NULL);

    g_autofree gchar *_encoded_text = g_paste_util_replace (text, "&", "&amp;");

    return g_paste_util_replace (_encoded_text, ">", "&gt;");
}

/**
 * g_paste_util_get_history_dir_path:
 *
 * Get the path to the directory where we store the history
 *
 * Returns: the directory path
 */
G_PASTE_VISIBLE gchar *
g_paste_util_get_history_dir_path (void)
{
    const gchar *user_data_dir = g_get_user_data_dir ();
    g_autofree gchar *legacy_path = g_build_filename (user_data_dir, PACKAGE_NAME, NULL);

    /* The history lives under PACKAGE ("gpaste"), except where a "GPaste"
     * directory (PACKAGE_NAME) is already there: that is where GPaste built by
     * a meson defining the two the other way round put it, and the history a
     * user has is worth more than the spelling of its directory. */
    if (g_file_test (legacy_path, G_FILE_TEST_IS_DIR))
        return g_steal_pointer (&legacy_path);

    return g_build_filename (user_data_dir, PACKAGE, NULL);
}

/**
 * g_paste_util_get_history_dir:
 *
 * Get the directory where we store the history
 *
 * Returns: (transfer full): the directory
 */
G_PASTE_VISIBLE GFile *
g_paste_util_get_history_dir (void)
{
    g_autofree gchar *history_dir_path = g_paste_util_get_history_dir_path ();

    return g_file_new_for_path (history_dir_path);
}

/**
 * g_paste_util_get_history_file_path:
 * @name: the name of the history
 * @extension: the file extension
 *
 * Get the path to the file in which we store the history
 *
 * Returns: the file path
 */
G_PASTE_VISIBLE gchar *
g_paste_util_get_history_file_path (const gchar *name,
                                    const gchar *extension)
{
    g_return_val_if_fail (name, NULL);
    g_return_val_if_fail (extension, NULL);

    g_autofree gchar *history_dir_path = g_paste_util_get_history_dir_path ();
    g_autofree gchar *history_file_name = g_strconcat (name, ".", extension, NULL);

    return g_build_filename (history_dir_path, history_file_name, NULL);
}

/**
 * g_paste_util_get_history_file:
 * @name: the name of the history
 * @extension: the file extension
 *
 * Get the file in which we store the history
 *
 * Returns: (transfer full): the file
 */
G_PASTE_VISIBLE GFile *
g_paste_util_get_history_file (const gchar *name,
                               const gchar *extension)
{
    g_return_val_if_fail (name, NULL);
    g_return_val_if_fail (extension, NULL);

    g_autofree gchar *history_file_path = g_paste_util_get_history_file_path (name, extension);

    return g_file_new_for_path (history_file_path);
}

/**
 * g_paste_util_ensure_history_dir_exists:
 *
 * Ensure the history dir exists
 *
 * Returns: where it exists or if there was an error creating it
 */
G_PASTE_VISIBLE gboolean
g_paste_util_ensure_history_dir_exists (void)
{
    g_autoptr (GFile) history_dir = g_paste_util_get_history_dir ();

    if (!g_file_query_exists (history_dir,
                              NULL)) /* cancellable */
    {
        g_autoptr (GError) error = NULL;

        g_file_make_directory_with_parents (history_dir,
                                            NULL, /* cancellable */
                                            &error);
        if (error)
        {
            g_critical ("%s: %s", _("Could not create history dir"), error->message);
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * g_paste_util_list_directory:
 * @dir: the directory to list
 * @attribute: the name attribute to read back off each entry, either
 *             %G_FILE_ATTRIBUTE_STANDARD_NAME or %G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME
 * @error: return location for a #GError, or %NULL
 *
 * List everything @dir holds, by name.
 *
 * A directory that is not there at all is not a failure: nothing has been
 * stored yet, and the answer is an empty list. Anything else is, and comes back
 * as %NULL — never a short list. Every caller acts on what it gets (deleting,
 * re-keying, importing, counting), so an enumeration that stopped half way must
 * not be told apart from a complete one by whoever reads it.
 *
 * Errors are the enumeration's own, so they land in %G_IO_ERROR.
 *
 * Returns: (transfer full) (nullable): the names, or %NULL
 */
G_PASTE_VISIBLE GStrv
g_paste_util_list_directory (GFile       *dir,
                             const gchar *attribute,
                             GError     **error)
{
    g_return_val_if_fail (G_IS_FILE (dir), NULL);
    g_return_val_if_fail (attribute, NULL);

    g_autoptr (GStrvBuilder) names = g_strv_builder_new ();
    g_autoptr (GError) local_error = NULL;
    g_autoptr (GFileEnumerator) children = g_file_enumerate_children (dir,
                                                                      attribute,
                                                                      G_FILE_QUERY_INFO_NONE,
                                                                      NULL, /* cancellable */
                                                                      &local_error);

    if (!children)
    {
        /* Nothing stored yet. */
        if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            return g_strv_builder_end (names);

        g_propagate_error (error, g_steal_pointer (&local_error));

        return NULL;
    }

    GFileInfo *info;

    while ((info = g_file_enumerator_next_file (children,
                                                NULL, /* cancellable */
                                                &local_error)))
    {
        g_autoptr (GFileInfo) child = info;

        /* STANDARD_NAME is a byte string and DISPLAY_NAME a UTF-8 one: each is
         * read back with its own getter rather than through the generic
         * get_attribute_as_string(), which escapes the bytes of the former. */
        const gchar *name = (g_file_info_get_attribute_type (child, attribute) == G_FILE_ATTRIBUTE_TYPE_BYTE_STRING)
            ? g_file_info_get_attribute_byte_string (child, attribute)
            : g_file_info_get_attribute_string (child, attribute);

        /* An entry the enumerator did not answer with @attribute at all. Adding
         * it would put a %NULL in the middle of the vector, which every caller
         * reads as the end of the listing: a short list wearing the shape of a
         * complete one, which is the one thing this must never return. */
        if (!name)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                         "A directory entry carries no \"%s\"", attribute);

            return NULL;
        }

        g_strv_builder_add (names, name);
    }

    /* next_file() returns NULL both at the end of the listing and on a failure,
     * so the two can only be told apart once the loop is over. */
    if (local_error)
    {
        g_propagate_error (error, g_steal_pointer (&local_error));

        return NULL;
    }

    return g_strv_builder_end (names);
}
