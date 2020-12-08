/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2021, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-content-provider.h>
#include <gpaste-image-item.h>
#include <gpaste-uris-item.h>

struct _GPasteContentProvider
{
    GdkContentProvider parent_instance;
};

enum
{
    C_SPECIAL_VALUE_ADDED,

    C_LAST_SIGNAL
};

typedef struct
{
    GPasteItem *item;

    guint64     c_signals[C_LAST_SIGNAL];
} GPasteContentProviderPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (ContentProvider, content_provider, GDK_TYPE_CONTENT_PROVIDER)

static void
g_paste_content_provider_reclaim (GPasteItem *item G_GNUC_UNUSED,
                                  gpointer      user_data)
{
    gdk_content_provider_content_changed (GDK_CONTENT_PROVIDER (user_data));
}

/**
 * g_paste_content_provider_set_item:
 * @item: a #GPasteItem instance
 *
 * Track the contents of a new item
 */
G_PASTE_VISIBLE void
g_paste_content_provider_set_item (GPasteContentProvider *self,
                                   GPasteItem            *item)
{
    g_return_if_fail (_G_PASTE_IS_CONTENT_PROVIDER (self));
    g_return_if_fail (_G_PASTE_IS_ITEM (item));

    GPasteContentProviderPrivate *priv = g_paste_content_provider_get_instance_private (self);
    g_autoptr (GPasteItem) previous = priv->item;

    if (previous != item && item && (!previous || !g_paste_item_equals (previous, item)))
    {
        if (previous)
            g_signal_handler_disconnect (previous, priv->c_signals[C_SPECIAL_VALUE_ADDED]);

        priv->item = g_object_ref (item);
        priv->c_signals[C_SPECIAL_VALUE_ADDED] = g_signal_connect (item,
                                                                   "special-value-added",
                                                                   G_CALLBACK (g_paste_content_provider_reclaim),
                                                                   self);
        gdk_content_provider_content_changed (GDK_CONTENT_PROVIDER (self));
    } else {
        previous = NULL;
    }
}

static GdkContentFormats *
g_paste_content_provider_ref_formats (GdkContentProvider *provider)
{
    const GPasteContentProvider *self = _G_PASTE_CONTENT_PROVIDER (provider);
    const GPasteContentProviderPrivate *priv = _g_paste_content_provider_get_instance_private (self);
    const GPasteItem *item = priv->item;
    GdkContentFormatsBuilder *formats = gdk_content_formats_builder_new ();

    if (item)
    {
        for (const GSList *special_value = g_paste_item_get_special_values (priv->item); special_value; special_value = special_value->next)
        {
            GPasteSpecialValue *v = special_value->data;
            gdk_content_formats_builder_add_mime_type (formats, v->mime);
        }
    }

    if (_G_PASTE_IS_IMAGE_ITEM (item))
        gdk_content_formats_builder_add_gtype (formats, GDK_TYPE_TEXTURE);
    else if (_G_PASTE_IS_URIS_ITEM (item))
        gdk_content_formats_builder_add_gtype (formats, GDK_TYPE_FILE_LIST);
    else
        gdk_content_formats_builder_add_gtype (formats, G_TYPE_STRING);

    return gdk_content_formats_builder_free_to_formats (formats);
}

static gboolean
g_paste_content_provider_get_value (GdkContentProvider  *provider,
                                    GValue              *value,
                                    GError             **error)
{
    const GPasteContentProvider *self = _G_PASTE_CONTENT_PROVIDER (provider);
    const GPasteContentProviderPrivate *priv = _g_paste_content_provider_get_instance_private (self);
    const GPasteItem *item = priv->item;

    if (item)
    {
        if (G_VALUE_HOLDS (value, GDK_TYPE_TEXTURE) && _G_PASTE_IS_IMAGE_ITEM (item))
        {
            g_value_set_instance (value, g_paste_image_item_get_image (_G_PASTE_IMAGE_ITEM (item)));
            return TRUE;
        }
        else if (G_VALUE_HOLDS (value, GDK_TYPE_FILE_LIST) && _G_PASTE_IS_URIS_ITEM (item))
        {
            g_value_set_boxed (value, g_paste_uris_item_get_files (_G_PASTE_URIS_ITEM (item)));
            return TRUE;
        }
        else if (G_VALUE_HOLDS (value, G_TYPE_STRING))
        {
            g_value_set_string (value, g_paste_item_get_real_value (item));
            return TRUE;
        }
    }

    return GDK_CONTENT_PROVIDER_CLASS (g_paste_content_provider_parent_class)->get_value (provider, value, error);
}

static void
g_paste_content_provider_write_mime_type_done (GObject      *stream,
                                               GAsyncResult *result,
                                               gpointer      user_data)
{
    g_autoptr (GTask) task = user_data;
    GError *error = NULL;

    if (!g_output_stream_write_all_finish (G_OUTPUT_STREAM (stream), result, NULL, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
}

static void
g_paste_content_provider_write_mime_type_async (GdkContentProvider  *provider,
                                                const char          *mime_type,
                                                GOutputStream       *stream,
                                                int                  io_priority,
                                                GCancellable        *cancellable,
                                                GAsyncReadyCallback  callback,
                                                gpointer             user_data)
{
    GPasteContentProvider *self = G_PASTE_CONTENT_PROVIDER (provider);
    const GPasteContentProviderPrivate *priv = _g_paste_content_provider_get_instance_private (self);
    const GPasteItem *item = priv->item;

    if (item)
    {
        for (const GSList *special_value = g_paste_item_get_special_values (item); special_value; special_value = special_value->next)
        {
            GPasteSpecialValue *v = special_value->data;
            if (v->mime == mime_type)
            {
                g_autoptr (GTask) task = g_task_new (self, cancellable, callback, user_data);
                g_autofree guchar *data = NULL;
                guint64 length = 0;

                data = g_base64_decode (v->data, &length);

                g_task_set_priority (task, io_priority);
                g_task_set_source_tag (task, g_paste_content_provider_write_mime_type_async);
                g_output_stream_write_all_async (stream,
                                                 data,
                                                 length,
                                                 io_priority,
                                                 cancellable,
                                                 g_paste_content_provider_write_mime_type_done,
                                                 task);
                task = NULL;
                return;
            }
        }
    }

    GDK_CONTENT_PROVIDER_CLASS (g_paste_content_provider_parent_class)->write_mime_type_async (provider, mime_type, stream, io_priority, cancellable, callback, user_data);
}

static gboolean
g_paste_content_provider_write_mime_type_finish (GdkContentProvider *provider,
                                                 GAsyncResult       *result,
                                                 GError            **error)
{
  g_return_val_if_fail (g_task_is_valid (result, provider), FALSE);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) == g_paste_content_provider_write_mime_type_async, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
g_paste_content_provider_dispose (GObject *object)
{
    GPasteContentProviderPrivate *priv = g_paste_content_provider_get_instance_private (G_PASTE_CONTENT_PROVIDER (object));

    if (priv->item)
    {
        g_signal_handler_disconnect (priv->item, priv->c_signals[C_SPECIAL_VALUE_ADDED]);
        g_clear_object (&priv->item);
    }

    G_OBJECT_CLASS (g_paste_content_provider_parent_class)->dispose (object);
}

static void
g_paste_content_provider_class_init (GPasteContentProviderClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GdkContentProviderClass *provider_class = GDK_CONTENT_PROVIDER_CLASS (klass);

    object_class->dispose = g_paste_content_provider_dispose;

    provider_class->ref_formats = g_paste_content_provider_ref_formats;
    provider_class->get_value = g_paste_content_provider_get_value;
    provider_class->write_mime_type_async = g_paste_content_provider_write_mime_type_async;
    provider_class->write_mime_type_finish = g_paste_content_provider_write_mime_type_finish;
}

static void
g_paste_content_provider_init (GPasteContentProvider *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_content_provider_new:
 *
 * Create a new instance of #GPasteContentProvider
 *
 * Returns: a newly allocated #GPasteContentProvider
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteContentProvider *
g_paste_content_provider_new (void)
{
    return G_PASTE_CONTENT_PROVIDER (g_object_new (G_PASTE_TYPE_CONTENT_PROVIDER, NULL));
}
