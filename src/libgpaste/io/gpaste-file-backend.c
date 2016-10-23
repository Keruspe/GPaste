/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-file-backend.h>
#include <gpaste-image-item.h>
#include <gpaste-util.h>

struct _GPasteFileBackend
{
    GPasteStorageBackend parent_instance;
};

G_PASTE_DEFINE_TYPE (FileBackend, file_backend, G_PASTE_TYPE_STORAGE_BACKEND)

static gchar *
g_paste_file_backend_encode (const gchar *text)
{
    g_autofree gchar *_encoded_text = g_paste_util_replace (text, "&", "&amp;");
    return g_paste_util_replace (_encoded_text, ">", "&gt;");
}

static GList *
g_paste_file_backend_read_history (const GPasteStorageBackend *self,
                                   const gchar                *source)
{
    /* TODO */
    return NULL;
}

static void
g_paste_file_backend_write_history (const GPasteStorageBackend *self G_GNUC_UNUSED,
                                    const gchar                *source,
                                    const GList                *history)
{
    g_autoptr (GFile) history_file = g_file_new_for_path (source);
    g_autoptr (GOutputStream) stream = G_OUTPUT_STREAM (g_file_replace (history_file,
                                                        NULL,
                                                        FALSE,
                                                        G_FILE_CREATE_REPLACE_DESTINATION,
                                                        NULL, /* cancellable */
                                                        NULL)); /* error */

    if (!g_output_stream_write_all (stream, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", 39, NULL, NULL /* cancellable */, NULL /* error */) ||
        !g_output_stream_write_all (stream, "<history version=\"1.0\">\n", 24, NULL, NULL /* cancellable */, NULL /* error */))
            return;

    for (; history; history = g_list_next (history))
    {
        GPasteItem *item = history->data;
        const gchar *kind = g_paste_item_get_kind (item);

        if (g_paste_str_equal (kind, "Password"))
            continue;

        g_autofree gchar *text = g_paste_file_backend_encode (g_paste_item_get_value (item));

        if (!g_output_stream_write_all (stream, "  <item kind=\"", 14, NULL, NULL /* cancellable */, NULL /* error */) ||
            !g_output_stream_write_all (stream, kind, strlen (kind), NULL, NULL /* cancellable */, NULL /* error */) ||
            (_G_PASTE_IS_IMAGE_ITEM (item) &&
                (!g_output_stream_write_all (stream, "\" date=\"", 8, NULL, NULL /* cancellable */, NULL /* error */) ||
                 !g_output_stream_write_all (stream, g_date_time_format ((GDateTime *) g_paste_image_item_get_date (G_PASTE_IMAGE_ITEM (item)), "%s"), 10, NULL, NULL /* cancellable */, NULL /* error */))) ||
            !g_output_stream_write_all (stream, "\"><![CDATA[", 11, NULL, NULL /* cancellable */, NULL /* error */) ||
            !g_output_stream_write_all (stream, text, strlen (text), NULL, NULL /* cancellable */, NULL /* error */) ||
            !g_output_stream_write_all (stream, "]]></item>\n", 11, NULL, NULL /* cancellable */, NULL /* error */))
        {
            g_warning ("Failed to write an item to history");
            continue;
        }
    }

    if (!g_output_stream_write_all (stream, "</history>\n", 11, NULL, NULL /* cancellable */, NULL /* error */) ||
        !g_output_stream_close (stream, NULL /* cancellable */, NULL /* error */))
            g_warning ("Failed to finish writing history");
}

static void
g_paste_file_backend_class_init (GPasteFileBackendClass *klass)
{
    GPasteStorageBackendClass *storage_class = G_PASTE_STORAGE_BACKEND_CLASS (klass);

    storage_class->read_history = g_paste_file_backend_read_history;
    storage_class->write_history = g_paste_file_backend_write_history;
}

static void
g_paste_file_backend_init (GPasteFileBackend *self G_GNUC_UNUSED)
{
}
