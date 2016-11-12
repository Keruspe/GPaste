/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-file-backend.h>
#include <gpaste-image-item.h>
#include <gpaste-password-item.h>
#include <gpaste-uris-item.h>
#include <gpaste-util.h>

G_PASTE_DEFINE_TYPE (FileBackend, file_backend, G_PASTE_TYPE_STORAGE_BACKEND)

/********************/
/* Begin XML Parser */
/********************/

typedef enum
{
    BEGIN,
    IN_HISTORY,
    IN_ITEM,
    HAS_TEXT,
    END
} State;

typedef enum
{
    TEXT,
    IMAGE,
    URIS,
    PASSWORD
} Type;

typedef struct
{
    GList   *history;
    gsize    mem_size;
    State    state;
    Type     type;
    guint64  current_size;
    guint64  max_size;
    gboolean images_support;
    gchar   *date;
    gchar   *name;
    gchar   *text;
} Data;

#define ASSERT_STATE(x)                                                                           \
    if (data->state != x)                                                                         \
    {                                                                                             \
        g_warning ("Expected state %" G_GINT32_FORMAT ", but got %" G_GINT32_FORMAT, x, data->state); \
        return;                                                                                   \
    }
#define SWITCH_STATE(x, y) \
    ASSERT_STATE (x);      \
    data->state = y

static void
start_tag (GMarkupParseContext *context G_GNUC_UNUSED,
           const gchar         *element_name,
           const gchar        **attribute_names,
           const gchar        **attribute_values,
           gpointer             user_data,
           GError             **error G_GNUC_UNUSED)
{
    Data *data = user_data;

    if (g_paste_str_equal (element_name, "history"))
    {
        SWITCH_STATE (BEGIN, IN_HISTORY);
    }
    else if (g_paste_str_equal (element_name, "item"))
    {
        SWITCH_STATE (IN_HISTORY, IN_ITEM);
        g_clear_pointer (&data->date, g_free);
        g_clear_pointer (&data->text, g_free);
        for (const gchar **a = attribute_names, **v = attribute_values; *a && *v; ++a, ++v)
        {
            if (g_paste_str_equal (*a, "kind"))
            {
                if (g_paste_str_equal (*v, "Text"))
                    data->type = TEXT;
                else if (g_paste_str_equal (*v, "Image"))
                    data->type = IMAGE;
                else if (g_paste_str_equal (*v, "Uris"))
                    data->type = URIS;
                else if (g_paste_str_equal (*v, "Password"))
                    data->type = PASSWORD;
                else
                    g_warning ("Unknown item kind: %s", *v);
            }
            else if (g_paste_str_equal (*a, "date"))
            {
                if (data->type != IMAGE)
                {
                    g_warning ("Expected type %" G_GINT32_FORMAT ", but got %" G_GINT32_FORMAT, IMAGE, data->type);
                    return;
                }
                data->date = g_strdup (*v);
            }
            else if (g_paste_str_equal (*a, "name"))
            {
                if (data->type != PASSWORD)
                {
                    g_warning ("Expected type %" G_GINT32_FORMAT ", but got %" G_GINT32_FORMAT, PASSWORD, data->type);
                    return;
                }
                data->name = g_strdup (*v);
            }
            else
                g_warning ("Unknown item attribute: %s", *a);
        }
    }
    else
        g_warning ("Unknown element: %s", element_name);
}

static void
end_tag (GMarkupParseContext *context G_GNUC_UNUSED,
         const gchar         *element_name,
         gpointer             user_data,
         GError             **error G_GNUC_UNUSED)
{
    Data *data = user_data;

    if (g_paste_str_equal (element_name, "history"))
    {
        SWITCH_STATE (IN_HISTORY, END);
    }
    else if (g_paste_str_equal (element_name, "item"))
    {
        SWITCH_STATE (HAS_TEXT, IN_HISTORY);
    }
    else
        g_warning ("Unknown element: %s", element_name);
}

static void
on_text (GMarkupParseContext *context G_GNUC_UNUSED,
         const gchar         *text,
         guint64              text_len,
         gpointer             user_data,
         GError             **error G_GNUC_UNUSED)
{
    Data *data = user_data;

    g_autofree gchar *txt = g_strndup (text, text_len);
    switch (data->state)
    {
    case IN_HISTORY:
    case HAS_TEXT:
        if (*g_strstrip (txt))
        {
            g_warning ("Unexpected text: %s", txt);
            return;
        }
        break;
    case IN_ITEM:
    {
        g_autofree gchar *value = g_paste_util_xml_decode (txt);
        if (*g_strstrip (txt))
        {
            if (data->current_size < data->max_size)
            {
                GPasteItem *item = NULL;

                switch (data->type)
                {
                case TEXT:
                    item = g_paste_text_item_new (value);
                    break;
                case URIS:
                    item = g_paste_uris_item_new (value);
                    break;
                case PASSWORD:
                    item = g_paste_password_item_new (data->name, value);
                    break;
                case IMAGE:
                    if (data->images_support && data->date)
                    {
                        g_autoptr (GDateTime) date_time = g_date_time_new_from_unix_local (g_ascii_strtoll (data->date,
                                                                                                            NULL, /* end */
                                                                                                            0)); /* base */
                        item = g_paste_image_item_new_from_file (value, date_time);
                    }
                    else
                    {
                        g_autoptr (GFile) img_file = g_file_new_for_path (value);

                        if (g_file_query_exists (img_file,
                                                 NULL)) /* cancellable */
                        {
                            g_file_delete (img_file,
                                           NULL, /* cancellable */
                                           NULL); /* error */
                        }
                    }
                    break;
                }

                if (item)
                {
                    data->mem_size += g_paste_item_get_size (item);
                    data->history = g_list_append (data->history, item);
                    ++data->current_size;;
                }
            }

            SWITCH_STATE (IN_ITEM, HAS_TEXT);
        }
        break;
    }
    default:
        g_warning ("Unexpected state: %" G_GINT32_FORMAT, data->state);
    }
}

static void on_error (GMarkupParseContext *context   G_GNUC_UNUSED,
                      GError              *error,
                      gpointer             user_data G_GNUC_UNUSED)
{
    g_warning ("error: %s", error->message);
}

/******************/
/* End XML Parser */
/******************/

static void
g_paste_file_backend_read_history_file (const GPasteStorageBackend *self,
                                        const gchar                *history_file_path,
                                        GList                     **history,
                                        gsize                      *size)
{
    const GPasteSettings *settings = _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->get_settings (self);
    g_autoptr (GFile) history_file = g_file_new_for_path (history_file_path);

    if (g_file_query_exists (history_file,
                             NULL)) /* cancellable */
    {
        GMarkupParser parser = {
            start_tag,
            end_tag,
            on_text,
            NULL,
            on_error
        };
        Data data = {
            NULL,
            0,
            BEGIN,
            TEXT,
            0,
            g_paste_settings_get_max_history_size (settings),
            g_paste_settings_get_images_support (settings),
            NULL,
            NULL,
            NULL
        };
        GMarkupParseContext *ctx = g_markup_parse_context_new (&parser,
                                                               G_MARKUP_TREAT_CDATA_AS_TEXT,
                                                               &data,
                                                               NULL);
        gchar *text;
        guint64 text_length;

        g_file_get_contents (history_file_path, &text, &text_length, NULL);
        g_markup_parse_context_parse (ctx, text, text_length, NULL);
        g_markup_parse_context_end_parse (ctx, NULL);

        if (data.state != END)
            g_warning ("Unexpected state adter parsing history: %" G_GINT32_FORMAT, data.state);
        g_markup_parse_context_unref (ctx);

        *history = data.history;
        *size = data.mem_size;
    }
    else
    {
        /* Create the empty file to be listed as an available history */
        if (g_paste_util_ensure_history_dir_exists (settings))
            g_object_unref (g_file_create (history_file, G_FILE_CREATE_NONE, NULL, NULL));
    }
}

static void
g_paste_file_backend_write_history_file (const GPasteStorageBackend *self,
                                         const gchar                *history_file_path,
                                         const GList                *history)
{
    const GPasteSettings *settings = _G_PASTE_STORAGE_BACKEND_GET_CLASS (self)->get_settings (self);

    if (!g_paste_util_ensure_history_dir_exists (settings))
        return;

    g_autoptr (GFile) history_file = g_file_new_for_path (history_file_path);

    if (!g_paste_settings_get_save_history (settings))
    {
        g_file_delete (history_file,
                       NULL, /* cancellable*/
                       NULL); /* error */

        return;
    }

    const GPasteFileBackend *real_self = _G_PASTE_FILE_BACKEND (self);
    g_autoptr (GOutputStream) stream = _G_PASTE_FILE_BACKEND_GET_CLASS (real_self)->get_output_stream (real_self, history_file);

    if (!g_output_stream_write_all (stream, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", 39, NULL, NULL /* cancellable */, NULL /* error */) ||
        !g_output_stream_write_all (stream, "<history version=\"1.0\">\n", 24, NULL, NULL /* cancellable */, NULL /* error */))
            return;

    for (; history; history = g_list_next (history))
    {
        GPasteItem *item = history->data;
        const gchar *kind = g_paste_item_get_kind (item);

        if (g_paste_str_equal (kind, "Password"))
            continue;

        g_autofree gchar *text = g_paste_util_xml_encode (g_paste_item_get_value (item));

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
    {
        g_warning ("Failed to finish writing history");
    }
}

static const gchar *
g_paste_file_backend_get_extension (const GPasteStorageBackend *self G_GNUC_UNUSED)
{
    return "xml";
}

static GOutputStream *
g_paste_file_backend_get_output_stream (const GPasteFileBackend *self G_GNUC_UNUSED,
                                        GFile                   *output_file)
{
    return G_OUTPUT_STREAM (g_file_replace (output_file,
                                            NULL,
                                            FALSE,
                                            G_FILE_CREATE_REPLACE_DESTINATION,
                                            NULL, /* cancellable */
                                            NULL)); /* error */
}

static void
g_paste_file_backend_class_init (GPasteFileBackendClass *klass)
{
    GPasteStorageBackendClass *storage_class = G_PASTE_STORAGE_BACKEND_CLASS (klass);

    storage_class->read_history_file = g_paste_file_backend_read_history_file;
    storage_class->write_history_file = g_paste_file_backend_write_history_file;
    storage_class->get_extension = g_paste_file_backend_get_extension;

    klass->get_output_stream = g_paste_file_backend_get_output_stream;
}

static void
g_paste_file_backend_init (GPasteFileBackend *self G_GNUC_UNUSED)
{
}
