// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include "gpaste-text-content-provider.h"

#include <string.h>

/* A content provider for text, equivalent to what
 * gdk_content_provider_new_typed (G_TYPE_STRING, text) gives us, except for
 * what a client asking for bare text/plain is served.
 *
 * GTK registers the text/plain serializer for a G_TYPE_STRING with "ASCII" as
 * its target charset and a '?' fallback (string_serializer () in
 * gdk/gdkcontentserializer.c), so a client that asks for text/plain rather than
 * for text/plain;charset=utf-8 gets a '?' in place of every character outside
 * ASCII. Claiming that one mime type and writing the utf-8 bytes into the
 * stream GDK owns is the whole of what this class is for.
 *
 * Serving utf-8 under that name is a convention rather than a guarantee: RFC
 * 2046 has a text media type default to US-ASCII, so a client is within its
 * rights to read those bytes as ASCII. It is the convention X11 clients follow,
 * and the alternative loses the content outright -- a '?' cannot be read back
 * as the character it stands for -- while a client that needs a charset it can
 * name asks for text/plain;charset=..., which the serializers answer.
 *
 * Everything else about the value stays GTK's: G_TYPE_STRING is still in our
 * formats, so a local reader gets it without a serialize/deserialize round
 * trip, and gdk_clipboard_set_content () unions that gtype with the mime types
 * the serializers provide, so text/plain;charset=utf-8 and the locale spelling
 * are still advertised and still served -- by them. What routes the two apart
 * is gdk_clipboard_write_async (), which hands a provider only the mime types
 * it claims itself.
 */

#define TEXT_PLAIN "text/plain"

struct _GPasteTextContentProvider
{
    GdkContentProvider parent_instance;

    gchar *text;
};

G_PASTE_DEFINE_TYPE (TextContentProvider, text_content_provider, GDK_TYPE_CONTENT_PROVIDER)

/* Claimed and no more: the gtype is what lets a local reader get our value
 * directly, and text/plain is the one mime type we serve ourselves. Every other
 * spelling the serializers advertise for a G_TYPE_STRING is left to them, which
 * is what claiming it here would take away. */
static GdkContentFormats *
g_paste_text_content_provider_ref_formats (GdkContentProvider *provider G_GNUC_UNUSED)
{
    GdkContentFormatsBuilder *builder = gdk_content_formats_builder_new ();

    gdk_content_formats_builder_add_gtype (builder, G_TYPE_STRING);
    gdk_content_formats_builder_add_mime_type (builder, TEXT_PLAIN);

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
g_paste_text_content_provider_write_mime_type_async (GdkContentProvider *provider,
                                                     const gchar        *mime_type,
                                                     GOutputStream      *stream,
                                                     gint                io_priority,
                                                     GCancellable       *cancellable,
                                                     GAsyncReadyCallback callback,
                                                     gpointer            user_data)
{
    GPasteTextContentProvider *self = G_PASTE_TEXT_CONTENT_PROVIDER (provider);
    GTask *task = g_task_new (provider, cancellable, callback, user_data);

    g_task_set_priority (task, io_priority);
    g_task_set_source_tag (task, g_paste_text_content_provider_write_mime_type_async);

    /* text/plain is the only mime type ref_formats () claims, so it is the only
     * one gdk_clipboard_write_async () ever routes here -- and what it is served
     * is the text as it stands, which is the whole point of claiming it. */
    if (!g_paste_str_equal (mime_type, TEXT_PLAIN))
    {
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                 "Cannot provide contents as \"%s\"", mime_type);
        g_object_unref (task);
        return;
    }

    /* The text as it stands, with nothing copied for the write to hold: the task
     * references the provider until the write is answered, and the text is the
     * provider's. */
    g_output_stream_write_all_async (stream,
                                     self->text,
                                     strlen (self->text),
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
