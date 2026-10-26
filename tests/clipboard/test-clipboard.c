// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-clipboard.h>

static GPasteClipboardContent
content_for (gboolean images_support,
             ...)
{
    g_autoptr (GdkContentFormatsBuilder) builder = gdk_content_formats_builder_new ();
    va_list types;

    va_start (types, images_support);
    for (GType type = va_arg (types, GType); type != G_TYPE_INVALID; type = va_arg (types, GType))
        gdk_content_formats_builder_add_gtype (builder, type);
    va_end (types);

    g_autoptr (GdkContentFormats) formats = gdk_content_formats_builder_to_formats (builder);

    return g_paste_clipboard_content_for_formats (formats, images_support);
}

/* A copy offering an image beside its text, as spreadsheet cells do, is read
 * as the text; an image alone is read as one. */
static void
test_text_before_image (void)
{
    g_assert_cmpint (content_for (TRUE, GDK_TYPE_TEXTURE, G_TYPE_STRING, G_TYPE_INVALID), ==, CLIPBOARD_CONTENT_TEXT);
    g_assert_cmpint (content_for (TRUE, G_TYPE_STRING, GDK_TYPE_TEXTURE, G_TYPE_INVALID), ==, CLIPBOARD_CONTENT_TEXT);
    g_assert_cmpint (content_for (TRUE, GDK_TYPE_TEXTURE, G_TYPE_INVALID), ==, CLIPBOARD_CONTENT_IMAGE);
    g_assert_cmpint (content_for (FALSE, GDK_TYPE_TEXTURE, G_TYPE_INVALID), ==, CLIPBOARD_CONTENT_IGNORED);
    g_assert_cmpint (content_for (TRUE, G_TYPE_INVALID), ==, CLIPBOARD_CONTENT_NONE);
    g_assert_cmpint (content_for (TRUE, GDK_TYPE_FILE_LIST, G_TYPE_STRING, GDK_TYPE_TEXTURE, G_TYPE_INVALID), ==, CLIPBOARD_CONTENT_FILE_LIST);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/clipboard/text-before-image", test_text_before_image);

    return g_test_run ();
}
