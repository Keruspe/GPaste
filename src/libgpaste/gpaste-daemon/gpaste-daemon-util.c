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
    g_autofree gchar *meson_bug_history_path = g_build_filename (user_data_dir, PACKAGE_NAME, NULL);

    // meson wrongfully defined PACKAGE as PACKAGE_NAME.
    // use it if it exists, but otherwise use the correct path.
    if (g_file_test (meson_bug_history_path, G_FILE_TEST_IS_DIR))
        return g_steal_pointer (&meson_bug_history_path);

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
