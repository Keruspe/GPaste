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

static gboolean
offers_bare_text (const gchar * const *mime_types)
{
    g_autoptr (GdkContentFormats) formats = gdk_content_formats_new ((const gchar **) mime_types, g_strv_length ((GStrv) mime_types));

    return g_paste_clipboard_offers_bare_text (formats);
}

/* A selection offering its text as bare text/plain alone is read as such,
 * rather than through GDK's text read, which decodes it as ASCII. */
static void
test_bare_text_offered (void)
{
    g_assert_true (offers_bare_text ((const gchar * const []) { "text/plain", NULL }));
    g_assert_true (offers_bare_text ((const gchar * const []) { "TARGETS", "text/plain", NULL }));
    g_assert_false (offers_bare_text ((const gchar * const []) { "text/plain;charset=utf-8", "text/plain", NULL }));
    g_assert_false (offers_bare_text ((const gchar * const []) { "text/plain;charset=utf-8", NULL }));
    g_assert_false (offers_bare_text ((const gchar * const []) { "text/html", NULL }));
}

static gchar *
text_from (const gchar *data,
           gsize        size,
           gboolean    *not_utf8)
{
    g_autoptr (GBytes) bytes = g_bytes_new (data, size);

    return g_paste_clipboard_text_from_bytes (bytes, not_utf8);
}

/* What a bare text/plain brings is UTF-8, up to a NUL ending it. */
static void
test_text_from_bytes (void)
{
    gboolean not_utf8 = TRUE;
    g_autofree gchar *accented = text_from ("caf\xc3\xa9", 5, &not_utf8);

    g_assert_cmpstr (accented, ==, "caf\xc3\xa9");
    g_assert_false (not_utf8);

    g_autofree gchar *ended = text_from ("abc\0junk", 8, &not_utf8);

    g_assert_cmpstr (ended, ==, "abc");
    g_assert_false (not_utf8);

    g_autofree gchar *latin1 = text_from ("caf\xe9", 4, &not_utf8);

    g_assert_null (latin1);
    g_assert_true (not_utf8);

    g_autofree gchar *empty = text_from (NULL, 0, &not_utf8);

    g_assert_cmpstr (empty, ==, "");
    g_assert_false (not_utf8);

    g_autofree gchar *failed = g_paste_clipboard_text_from_bytes (NULL, &not_utf8);

    g_assert_null (failed);
    g_assert_false (not_utf8);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/clipboard/text-before-image", test_text_before_image);
    g_test_add_func ("/clipboard/bare-text-offered", test_bare_text_offered);
    g_test_add_func ("/clipboard/text-from-bytes", test_text_from_bytes);

    return g_test_run ();
}
