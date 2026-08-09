// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include "gpaste-text-content-provider.h"

#include <string.h>

/* A content provider for text, equivalent to what
 * gdk_content_provider_new_typed (G_TYPE_STRING, text) gives us, except that
 * it never lets GTK serialize the value.
 *
 * GTK serializes a G_TYPE_STRING through a GConverterOutputStream that nobody
 * owns: the only reference left is the one held by the GTask of the write it
 * starts, so the whole stream chain is finalized from the main loop dispatch
 * which completes that write. GIO's dispose then closes the unclosed chain
 * synchronously, and on X11 that ends up in
 * gdk_x11_selection_output_stream_flush() waiting on a GCond only this very
 * main loop could signal: the daemon deadlocks against itself, most often on
 * the larger items, which go through INCR.
 *
 * Encoding the text ourselves and writing it straight into the stream GDK
 * owns (and closes asynchronously) leaves no unowned stream chain behind,
 * while keeping G_TYPE_STRING in our formats: local readers still get the
 * value directly instead of going through a serialize/deserialize round trip.
 */

#define TEXT_PLAIN         "text/plain"
#define TEXT_PLAIN_UTF8    "text/plain;charset=utf-8"
#define TEXT_PLAIN_CHARSET "text/plain;charset="

struct _GPasteTextContentProvider
{
    GdkContentProvider parent_instance;

    gchar *text;
};

G_PASTE_DEFINE_TYPE (TextContentProvider, text_content_provider, GDK_TYPE_CONTENT_PROVIDER)

/* The gtype is what lets local readers get our value directly, but the mime
 * types have to be advertised here as well: gdk_clipboard_write_async only
 * hands us the mime types we claim ourselves, and serializes the value (the
 * very path we're avoiding) for all the others. So claim exactly what the
 * G_TYPE_STRING serializers would have provided, and handle all of it in
 * g_paste_text_content_provider_encode.
 */
static GdkContentFormats *
g_paste_text_content_provider_ref_formats (GdkContentProvider *provider G_GNUC_UNUSED)
{
    GdkContentFormatsBuilder *builder = gdk_content_formats_builder_new ();
    const gchar *charset;

    gdk_content_formats_builder_add_gtype (builder, G_TYPE_STRING);
    gdk_content_formats_builder_add_mime_type (builder, TEXT_PLAIN_UTF8);
    gdk_content_formats_builder_add_mime_type (builder, TEXT_PLAIN);

    if (!g_get_charset (&charset))
    {
        g_autofree gchar *mime_type = g_strconcat (TEXT_PLAIN_CHARSET, charset, NULL);

        gdk_content_formats_builder_add_mime_type (builder, mime_type);
    }

    return gdk_content_formats_builder_free_to_formats (builder);
}

static gboolean
g_paste_text_content_provider_get_value (GdkContentProvider *provider,
                                         GValue             *value,
                                         GError            **error)
{
    GPasteTextContentProvider *self = G_PASTE_TEXT_CONTENT_PROVIDER (provider);

    if (G_VALUE_HOLDS (value, G_TYPE_STRING))
    {
        g_value_set_string (value, self->text);
        return TRUE;
    }

    return GDK_CONTENT_PROVIDER_CLASS (g_paste_text_content_provider_parent_class)->get_value (provider, value, error);
}

/* Anything the clipboard advertises on our behalf has to be handled here:
 * gdk_clipboard_set_content unions our G_TYPE_STRING with the mime types the
 * serializers would provide, which is text/plain, text/plain;charset=utf-8
 * and, on a non utf-8 locale, text/plain;charset=<locale>.
 *
 * text/plain gets the utf-8 bytes as everyone expects nowadays, where GTK
 * would encode it as ASCII and replace anything else with '?'.
 */
static GBytes *
g_paste_text_content_provider_encode (const gchar *text,
                                      const gchar *mime_type)
{
    if (g_paste_str_equal (mime_type, TEXT_PLAIN_UTF8) ||
        g_paste_str_equal (mime_type, TEXT_PLAIN))
            return g_bytes_new (text, strlen (text));

    if (!g_str_has_prefix (mime_type, TEXT_PLAIN_CHARSET))
        return NULL;

    g_autoptr (GError) error = NULL;
    gsize length;
    gchar *converted = g_convert_with_fallback (text,
                                                -1,
                                                mime_type + strlen (TEXT_PLAIN_CHARSET), /* to */
                                                "utf-8",                                 /* from */
                                                "?",
                                                NULL, /* bytes read */
                                                &length,
                                                &error);

    if (!converted)
    {
        g_debug ("Failed to provide text as %s: %s", mime_type, error->message);
        return NULL;
    }

    return g_bytes_new_take (converted, length);
}

static void
g_paste_text_content_provider_write_done (GObject      *source_object,
                                          GAsyncResult *result,
                                          gpointer      user_data)
{
    g_autoptr (GTask) task = user_data;
    g_autoptr (GError) error = NULL;

    if (!g_output_stream_write_all_finish (G_OUTPUT_STREAM (source_object), result, NULL, &error))
        g_task_return_error (task, g_steal_pointer (&error));
    else
        g_task_return_boolean (task, TRUE);
}

static void
g_paste_text_content_provider_write_mime_type_async (GdkContentProvider  *provider,
                                                     const gchar         *mime_type,
                                                     GOutputStream       *stream,
                                                     gint                 io_priority,
                                                     GCancellable        *cancellable,
                                                     GAsyncReadyCallback  callback,
                                                     gpointer             user_data)
{
    GPasteTextContentProvider *self = G_PASTE_TEXT_CONTENT_PROVIDER (provider);
    GTask *task = g_task_new (provider, cancellable, callback, user_data);

    g_task_set_priority (task, io_priority);
    g_task_set_source_tag (task, g_paste_text_content_provider_write_mime_type_async);

    GBytes *bytes = g_paste_text_content_provider_encode (self->text, mime_type);

    if (!bytes)
    {
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                 "Cannot provide contents as \"%s\"", mime_type);
        g_object_unref (task);
        return;
    }

    gsize length;
    gconstpointer data = g_bytes_get_data (bytes, &length);

    /* Keep the bytes around for as long as we're writing them */
    g_task_set_task_data (task, bytes, (GDestroyNotify) g_bytes_unref);

    g_output_stream_write_all_async (stream,
                                     data,
                                     length,
                                     io_priority,
                                     cancellable,
                                     g_paste_text_content_provider_write_done,
                                     task);
}

static gboolean
g_paste_text_content_provider_write_mime_type_finish (GdkContentProvider *provider,
                                                      GAsyncResult       *result,
                                                      GError            **error)
{
    g_return_val_if_fail (g_task_is_valid (result, provider), FALSE);
    g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) == g_paste_text_content_provider_write_mime_type_async, FALSE);

    return g_task_propagate_boolean (G_TASK (result), error);
}

static void
g_paste_text_content_provider_finalize (GObject *object)
{
    GPasteTextContentProvider *self = G_PASTE_TEXT_CONTENT_PROVIDER (object);

    g_free (self->text);

    G_OBJECT_CLASS (g_paste_text_content_provider_parent_class)->finalize (object);
}

static void
g_paste_text_content_provider_class_init (GPasteTextContentProviderClass *klass)
{
    GdkContentProviderClass *provider_class = GDK_CONTENT_PROVIDER_CLASS (klass);

    G_OBJECT_CLASS (klass)->finalize = g_paste_text_content_provider_finalize;

    provider_class->ref_formats = g_paste_text_content_provider_ref_formats;
    provider_class->get_value = g_paste_text_content_provider_get_value;
    provider_class->write_mime_type_async = g_paste_text_content_provider_write_mime_type_async;
    provider_class->write_mime_type_finish = g_paste_text_content_provider_write_mime_type_finish;
}

static void
g_paste_text_content_provider_init (GPasteTextContentProvider *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_text_content_provider_new:
 * @text: the text to provide
 *
 * Create a new instance of #GPasteTextContentProvider
 *
 * Returns: (transfer full): a newly allocated #GdkContentProvider
 *          free it with g_object_unref
 */
GdkContentProvider *
g_paste_text_content_provider_new (const gchar *text)
{
    g_return_val_if_fail (text, NULL);

    GPasteTextContentProvider *self = g_object_new (G_PASTE_TYPE_TEXT_CONTENT_PROVIDER, NULL);

    self->text = g_strdup (text);

    return GDK_CONTENT_PROVIDER (self);
}
