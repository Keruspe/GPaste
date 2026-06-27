// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <string.h>

#include <gpaste-daemon/gpaste-clipboard-content.h>

/**
 * g_paste_clipboard_content_clear:
 * @content: a #GPasteClipboardContent
 *
 * Release whatever @content currently holds and reset it to %CLIPBOARD_CONTENT_NONE.
 */
void
g_paste_clipboard_content_clear (GPasteClipboardContent *content)
{
    switch (content->kind)
    {
    case CLIPBOARD_CONTENT_TEXT:
    case CLIPBOARD_CONTENT_IMAGE:
        g_clear_pointer (&content->str, g_free);
        break;
    case CLIPBOARD_CONTENT_FILE_LIST:
        if (content->file_list)
            g_boxed_free (GDK_TYPE_FILE_LIST, g_steal_pointer (&content->file_list));
        break;
    case CLIPBOARD_CONTENT_COLOR:
    case CLIPBOARD_CONTENT_NONE:
        break;
    }
    content->kind = CLIPBOARD_CONTENT_NONE;
}

/**
 * g_paste_clipboard_content_is_empty:
 * @content: a #GPasteClipboardContent
 *
 * Returns: whether @content holds nothing
 */
gboolean
g_paste_clipboard_content_is_empty (const GPasteClipboardContent *content)
{
    return content->kind == CLIPBOARD_CONTENT_NONE;
}

/**
 * g_paste_clipboard_content_set_text:
 * @content: a #GPasteClipboardContent
 * @text: the text to hold (copied)
 *
 * Replace @content with @text.
 */
void
g_paste_clipboard_content_set_text (GPasteClipboardContent *content,
                                    const gchar            *text)
{
    g_paste_clipboard_content_clear (content);
    content->kind = CLIPBOARD_CONTENT_TEXT;
    content->str = g_strdup (text);
}

/**
 * g_paste_clipboard_content_set_image_checksum:
 * @content: a #GPasteClipboardContent
 * @checksum: the image checksum to hold (copied)
 *
 * Replace @content with an image identified by @checksum.
 */
void
g_paste_clipboard_content_set_image_checksum (GPasteClipboardContent *content,
                                              const gchar            *checksum)
{
    g_paste_clipboard_content_clear (content);
    content->kind = CLIPBOARD_CONTENT_IMAGE;
    content->str = g_strdup (checksum);
}

/**
 * g_paste_clipboard_content_set_color:
 * @content: a #GPasteClipboardContent
 * @rgba: the colour to hold (copied)
 *
 * Replace @content with the colour @rgba.
 */
void
g_paste_clipboard_content_set_color (GPasteClipboardContent *content,
                                     const GdkRGBA          *rgba)
{
    g_paste_clipboard_content_clear (content);
    content->kind = CLIPBOARD_CONTENT_COLOR;
    content->rgba = *rgba;
}

/**
 * g_paste_clipboard_content_set_file_list:
 * @content: a #GPasteClipboardContent
 * @file_list: (nullable): the file list to hold (copied)
 *
 * Replace @content with @file_list. A %NULL @file_list leaves @content empty.
 */
void
g_paste_clipboard_content_set_file_list (GPasteClipboardContent *content,
                                         GdkFileList            *file_list)
{
    g_paste_clipboard_content_clear (content);

    if (!file_list)
        return;

    content->kind = CLIPBOARD_CONTENT_FILE_LIST;
    content->file_list = g_boxed_copy (GDK_TYPE_FILE_LIST, file_list);
}

/**
 * g_paste_clipboard_content_get_text:
 * @content: a #GPasteClipboardContent
 *
 * Returns: (nullable): the held text, or %NULL unless @content holds text
 */
const gchar *
g_paste_clipboard_content_get_text (const GPasteClipboardContent *content)
{
    return (content->kind == CLIPBOARD_CONTENT_TEXT) ? content->str : NULL;
}

/**
 * g_paste_clipboard_content_get_image_checksum:
 * @content: a #GPasteClipboardContent
 *
 * Returns: (nullable): the held image checksum, or %NULL unless @content holds an image
 */
const gchar *
g_paste_clipboard_content_get_image_checksum (const GPasteClipboardContent *content)
{
    return (content->kind == CLIPBOARD_CONTENT_IMAGE) ? content->str : NULL;
}

/**
 * g_paste_clipboard_content_classify_text:
 * @content: the currently held content (for dedup against the new text)
 * @settings: a #GPasteSettings instance (trim and min/max size policy)
 * @is_clipboard: whether the caller drives the clipboard (vs the primary selection)
 * @text: the candidate text just read from the selection
 * @out_value: (out) (transfer full): the text to act on, or %NULL when rejected
 *
 * Apply the shared trim/size/dedup policy to a candidate clipboard text, so both
 * backends accept and normalise text identically.
 *
 * Returns: the action the backend should take for @text
 */
GPasteClipboardTextAction
g_paste_clipboard_content_classify_text (const GPasteClipboardContent *content,
                                         const GPasteSettings         *settings,
                                         gboolean                      is_clipboard,
                                         const gchar                  *text,
                                         gchar                       **out_value)
{
    gboolean trim_items = g_paste_settings_get_trim_items (settings);
    g_autofree gchar *stripped = trim_items ? g_strstrip (g_strdup (text)) : NULL;
    const gchar *to_add = trim_items ? stripped : text;
    guint64 length = strlen (to_add);

    *out_value = NULL;

    if (length < g_paste_settings_get_min_text_item_size (settings) ||
        length > g_paste_settings_get_max_text_item_size (settings))
        return G_PASTE_CLIPBOARD_TEXT_REJECT;

    if (content->kind == CLIPBOARD_CONTENT_TEXT && g_paste_str_equal (content->str, to_add))
        return G_PASTE_CLIPBOARD_TEXT_REJECT;

    /* Trimming changed the clipboard's own text: re-own it with the stripped form. */
    if (trim_items && is_clipboard && !g_paste_str_equal (text, stripped))
    {
        *out_value = g_steal_pointer (&stripped);
        return G_PASTE_CLIPBOARD_TEXT_RESELECT;
    }

    /* When trimming, to_add aliases the owned stripped buffer — hand it off rather
     * than copying; otherwise to_add borrows text and must be duplicated. */
    *out_value = trim_items ? g_steal_pointer (&stripped) : g_strdup (text);
    return G_PASTE_CLIPBOARD_TEXT_SET;
}

/**
 * g_paste_clipboard_file_list_equal:
 * @a: (nullable): a #GdkFileList
 * @b: (nullable): another #GdkFileList
 *
 * Returns: whether @a and @b list the same files in the same order
 */
gboolean
g_paste_clipboard_file_list_equal (GdkFileList *a,
                                   GdkFileList *b)
{
    if (a == b)
        return TRUE;
    if (!a || !b)
        return FALSE;

    GSList *fa = gdk_file_list_get_files (a);
    GSList *fb = gdk_file_list_get_files (b);

    for (; fa && fb; fa = fa->next, fb = fb->next)
    {
        if (!g_file_equal (G_FILE (fa->data), G_FILE (fb->data)))
            return FALSE;
    }

    return !fa && !fb;
}
