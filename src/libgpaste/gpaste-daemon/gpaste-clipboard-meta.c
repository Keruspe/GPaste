// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <stdlib.h>
#include <string.h>

#include <gdk/gdk.h>

#include <gpaste-daemon/gpaste-binary-data.h>
#include <gpaste-daemon/gpaste-clipboard-content.h>
#include <gpaste-daemon/gpaste-clipboard-meta.h>
#include <gpaste-daemon/gpaste-color-item.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-sensitive-mime.h>
#include <gpaste-daemon/gpaste-special-mime.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-uris-item.h>

/*
 * MetaSelection-backed clipboard provider.
 *
 * Unlike the GDK backend, this one is not a Wayland/X11 client: it talks to
 * mutter's server-side selection tracker directly, which is only reachable from
 * inside the gnome-shell process. It therefore takes the #MetaSelection handed
 * out by global.display.get_selection() (passed in by the shell glue) rather
 * than opening a display connection of its own, and it sees *every* selection
 * ownership change globally — no keyboard-focus gating, unlike GdkClipboard.
 *
 * Reads go through meta_selection_transfer_async() into an in-memory stream;
 * writes publish a #GPasteClipboardMetaSource we own (and recognise on the
 * resulting owner-change to avoid reprocessing our own writes). Unlike mutter's
 * #MetaSelectionSourceMemory, which only ever advertises a single mimetype, our
 * source holds several (mimetype, bytes) pairs so a text/uris item can offer its
 * rich-text/HTML/XML special values alongside the plain payload, matching what
 * the GDK backend does through a #GdkContentProvider union.
 */

#define META_MIME_TEXT       "text/plain;charset=utf-8"
#define META_MIME_TEXT_PLAIN "text/plain"
/* Read-side preferences only: the canonical representation we'd rather pull when
 * several are offered (lossless PNG, the plain uri-list over a portal roundtrip).
 * The set of formats we accept and advertise is whatever GDK can (de)serialise:
 * in gnome-shell, exactly these, which g_paste_clipboard_meta_ensure_formats ()
 * gives it. */
#define META_MIME_IMAGE      "image/png"
#define META_MIME_URIS       "text/uri-list"

struct _GPasteClipboardMeta
{
    GObject parent_instance;

    MetaSelection         *selection;
    MetaSelectionType      type;
    gboolean               is_clipboard;
    GPasteSettings        *settings;
    MetaSelectionSource   *owned_source;
    gulong                 owner_changed_id;

    GPasteClipboardContent content;

    /* The update in flight on this selection, cleared when it concludes: see
     * @slot on #GPasteClipboardUpdate. */
    GPasteClipboardUpdate *update;
};

static void g_paste_clipboard_meta_provider_iface_init (GPasteClipboardProviderInterface *iface);

G_PASTE_DEFINE_TYPE_WITH_INTERFACE (ClipboardMeta, clipboard_meta, G_TYPE_OBJECT,
                                    G_PASTE_TYPE_CLIPBOARD_PROVIDER, g_paste_clipboard_meta_provider_iface_init)

static gboolean
g_paste_clipboard_meta_is_clipboard (GPasteClipboardMeta *self)
{
    return self->is_clipboard;
}

static gboolean
g_paste_clipboard_meta_is_reading (GPasteClipboardMeta *self)
{
    return self->update != NULL;
}

static const gchar *
g_paste_clipboard_meta_get_text (GPasteClipboardMeta *self)
{
    return g_paste_clipboard_content_get_text (&self->content);
}

static const gchar *
g_paste_clipboard_meta_get_image_checksum (GPasteClipboardMeta *self)
{
    return g_paste_clipboard_content_get_image_checksum (&self->content);
}

static gboolean
g_paste_clipboard_meta_is_identified (GPasteClipboardMeta *self)
{
    return g_paste_clipboard_content_is_identified (&self->content);
}

/* --- mimetype helpers --- */

static gboolean
mimetypes_contain (GList       *mimetypes,
                   const gchar *mime)
{
    for (GList *m = mimetypes; m; m = m->next)
    {
        if (g_paste_str_equal (m->data, mime))
            return TRUE;
    }

    return FALSE;
}

/* --- async byte reads via meta_selection_transfer_async --- */

typedef void (*GPasteClipboardMetaBytesCallback) (GPasteClipboardMeta *self,
                                                  GBytes              *bytes,
                                                  gpointer             user_data);

typedef struct
{
    GPasteClipboardMeta             *self;
    GOutputStream                   *ostream;
    gsize                            limit;
    GPasteClipboardMetaBytesCallback callback;
    gpointer                         user_data;
} GPasteClipboardMetaReadData;

static void
g_paste_clipboard_meta_on_transfer_done (GObject      *source_object,
                                         GAsyncResult *res,
                                         gpointer      user_data)
{
    g_autofree GPasteClipboardMetaReadData *data = user_data;
    g_autoptr (GPasteClipboardMeta) self = data->self; /* ref taken in read_mime */
    g_autoptr (GOutputStream) ostream = data->ostream;
    g_autoptr (GError) error = NULL;

    if (!meta_selection_transfer_finish (META_SELECTION (source_object), res, &error))
    {
        if (error)
            g_debug ("Failed to read selection content: %s", error->message);
        if (data->callback)
            data->callback (self, NULL, data->user_data);
        return;
    }

    /* steal_as_bytes requires a closed stream and the transfer leaves it open.
     * Closing a #GMemoryOutputStream cannot fail, hence the unchecked error. */
    g_output_stream_close (ostream, NULL, NULL);

    g_autoptr (GBytes) bytes = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (ostream));

    if (g_bytes_get_size (bytes) > data->limit)
    {
        g_debug ("Ignoring selection content larger than the memory limit");
        g_clear_pointer (&bytes, g_bytes_unref);
    }

    if (data->callback)
        data->callback (self, bytes, data->user_data);
}

/* @mib in bytes, made in 64 bits and clamped there, before it narrows to a
 * gsize: on a 32-bit build, 4096 MiB or a multiple of it would wrap to 0, and
 * every transfer be cut at its first byte and dropped. The schema allows up to
 * 16383 MiB; no test builds 32-bit to show it. */
static gsize
g_paste_clipboard_meta_transfer_limit (guint64 mib)
{
    return (gsize) MIN (mib * 1024 * 1024, (guint64) G_MAXSSIZE - 1);
}

static void
g_paste_clipboard_meta_read_mime (GPasteClipboardMeta             *self,
                                  const gchar                     *mimetype,
                                  GCancellable                    *cancellable,
                                  GPasteClipboardMetaBytesCallback callback,
                                  gpointer                         user_data)
{
    GPasteClipboardMetaReadData *data = g_new0 (GPasteClipboardMetaReadData, 1);

    /* Hold a ref for the duration of the async transfer so the provider cannot be
     * finalized out from under the callback (released in on_transfer_done). */
    data->self = g_object_ref (self);
    data->ostream = g_memory_output_stream_new_resizable ();
    /* Bounded, as nothing else bounds what an owner serves, inside the
     * compositor: an endless offer would have it buffer until the read guard's
     * deadline. Nothing larger than the history's memory limit could be kept
     * anyway, a secret included, so one byte past it is all a read takes, to
     * tell such a content from one that just fits. */
    data->limit = g_paste_clipboard_meta_transfer_limit (g_paste_settings_get_max_memory_usage (self->settings));
    data->callback = callback;
    data->user_data = user_data;

    meta_selection_transfer_async (self->selection,
                                   self->type,
                                   mimetype,
                                   (gssize) MIN (data->limit + 1, (gsize) G_MAXSSIZE),
                                   data->ostream,
                                   cancellable,
                                   g_paste_clipboard_meta_on_transfer_done,
                                   data);
}

/* --- the formats GPaste needs, where GTK registered none --- */

/*
 * GTK registers GDK's (de)serializers in gtk_init (), which gnome-shell -- this
 * backend's only host -- never calls: GDK then knows no format at all, and no
 * image, colour or file list would be read or published. Where GDK has none for
 * one of the three types, the one format GPaste stores it as is registered for
 * it instead: image/png, application/x-color and text/uri-list. A host that did
 * initialise GTK keeps GDK's own, with every other format they bring.
 */
typedef void (*GPasteClipboardMetaDecode) (GValue  *value,
                                           GBytes  *bytes,
                                           GError **error);

static GPasteClipboardMetaDecode g_paste_clipboard_meta_own_decode (const gchar *mimetype);

static void
g_paste_clipboard_meta_on_format_read (GObject      *source_object,
                                       GAsyncResult *res,
                                       gpointer      user_data)
{
    GdkContentDeserializer *deserializer = user_data;
    GError *error = NULL;

    if (g_output_stream_splice_finish (G_OUTPUT_STREAM (source_object), res, &error) >= 0)
    {
        g_autoptr (GBytes) bytes = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (source_object));
        GPasteClipboardMetaDecode decode = (GPasteClipboardMetaDecode) gdk_content_deserializer_get_user_data (deserializer);

        decode (gdk_content_deserializer_get_value (deserializer), bytes, &error);
    }

    if (error)
        gdk_content_deserializer_return_error (deserializer, error);
    else
        gdk_content_deserializer_return_success (deserializer);
}

static void
g_paste_clipboard_meta_deserialize (GdkContentDeserializer *deserializer)
{
    g_autoptr (GOutputStream) ostream = g_memory_output_stream_new_resizable ();

    g_output_stream_splice_async (ostream,
                                  gdk_content_deserializer_get_input_stream (deserializer),
                                  G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
                                  gdk_content_deserializer_get_priority (deserializer),
                                  gdk_content_deserializer_get_cancellable (deserializer),
                                  g_paste_clipboard_meta_on_format_read,
                                  deserializer);
}

static void
g_paste_clipboard_meta_decode_texture (GValue  *value,
                                       GBytes  *bytes,
                                       GError **error)
{
    GdkTexture *texture = gdk_texture_new_from_bytes (bytes, error);

    if (texture)
        g_value_take_object (value, texture);
}

/* Four native-endian 16-bit channels, as GTK writes and reads them. */
static void
g_paste_clipboard_meta_decode_rgba (GValue  *value,
                                    GBytes  *bytes,
                                    GError **error)
{
    gsize size = 0;
    const guint16 *channels = g_bytes_get_data (bytes, &size);

    if (size != 4 * sizeof (guint16))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "A colour is four 16-bit channels");
        return;
    }

    GdkRGBA rgba = {
        .red = channels[0] / 65535.,
        .green = channels[1] / 65535.,
        .blue = channels[2] / 65535.,
        .alpha = channels[3] / 65535.,
    };

    g_value_set_boxed (value, &rgba);
}

static void
g_paste_clipboard_meta_decode_file_list (GValue  *value,
                                         GBytes  *bytes,
                                         GError **error)
{
    GdkFileList *file_list = g_paste_clipboard_file_list_from_bytes (bytes);

    if (file_list)
        g_value_take_boxed (value, file_list);
    else
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "More than %u files", G_PASTE_CLIPBOARD_MAX_FILES);
}

static void
g_paste_clipboard_meta_on_format_written (GObject      *source_object,
                                          GAsyncResult *res,
                                          gpointer      user_data)
{
    GdkContentSerializer *serializer = user_data;
    GError *error = NULL;

    if (g_output_stream_write_all_finish (G_OUTPUT_STREAM (source_object), res, NULL, &error))
        gdk_content_serializer_return_success (serializer);
    else
        gdk_content_serializer_return_error (serializer, error);
}

static void
g_paste_clipboard_meta_serialize (GdkContentSerializer *serializer)
{
    const GValue *value = gdk_content_serializer_get_value (serializer);
    g_autoptr (GBytes) bytes = NULL;

    if (G_VALUE_HOLDS (value, GDK_TYPE_TEXTURE))
        bytes = gdk_texture_save_to_png_bytes (g_value_get_object (value));
    else if (G_VALUE_HOLDS (value, GDK_TYPE_RGBA))
    {
        const GdkRGBA *rgba = g_value_get_boxed (value);
        guint16 channels[] = {
            (guint16) (CLAMP (rgba->red, 0., 1.) * 65535.),
            (guint16) (CLAMP (rgba->green, 0., 1.) * 65535.),
            (guint16) (CLAMP (rgba->blue, 0., 1.) * 65535.),
            (guint16) (CLAMP (rgba->alpha, 0., 1.) * 65535.),
        };

        bytes = g_bytes_new (channels, sizeof (channels));
    }
    else
    {
        g_auto (GStrv) uris = g_paste_clipboard_file_list_get_uris (g_value_get_boxed (value));
        g_autoptr (GString) list = g_string_new (NULL);

        for (GStrv uri = uris; *uri; ++uri)
            g_string_append_printf (list, "%s\r\n", *uri);
        bytes = g_string_free_to_bytes (g_steal_pointer (&list));
    }

    /* Kept with the serializer until the write it is the buffer of lands. */
    gdk_content_serializer_set_task_data (serializer, g_bytes_ref (bytes), (GDestroyNotify) g_bytes_unref);
    g_output_stream_write_all_async (gdk_content_serializer_get_output_stream (serializer),
                                     g_bytes_get_data (bytes, NULL),
                                     g_bytes_get_size (bytes),
                                     gdk_content_serializer_get_priority (serializer),
                                     gdk_content_serializer_get_cancellable (serializer),
                                     g_paste_clipboard_meta_on_format_written,
                                     serializer);
}

/* The formats g_paste_clipboard_meta_ensure_formats () offers to give GDK, and
 * which of them it did, GDK having none of its own: a selection's bytes in one
 * of those are decoded here, straight off the transfer. */
static struct
{
    GType                     type;
    const gchar              *mimetype;
    GPasteClipboardMetaDecode decode;
    gboolean                  registered;
} g_paste_clipboard_meta_formats[] = {
    { G_TYPE_INVALID, META_MIME_IMAGE,       g_paste_clipboard_meta_decode_texture,   FALSE },
    { G_TYPE_INVALID, "application/x-color", g_paste_clipboard_meta_decode_rgba,      FALSE },
    { G_TYPE_INVALID, META_MIME_URIS,        g_paste_clipboard_meta_decode_file_list, FALSE },
};

static GPasteClipboardMetaDecode
g_paste_clipboard_meta_own_decode (const gchar *mimetype)
{
    for (guint i = 0; i < G_N_ELEMENTS (g_paste_clipboard_meta_formats); ++i)
    {
        if (g_paste_clipboard_meta_formats[i].registered && g_paste_str_equal (g_paste_clipboard_meta_formats[i].mimetype, mimetype))
            return g_paste_clipboard_meta_formats[i].decode;
    }

    return NULL;
}

/* The formats GPaste stores each type as, for a host that never initialised
 * GTK. A file list then offers text/uri-list alone: the types sandboxed
 * applications take files through (application/vnd.portal.filetransfer,
 * application/vnd.portal.files) are GTK's own, registered against the document
 * portal by gtk_init (), which nothing in gnome-shell calls. */
static void
g_paste_clipboard_meta_ensure_formats (void)
{
    static gsize registered = 0;

    if (g_once_init_enter (&registered))
    {
        g_paste_clipboard_meta_formats[0].type = GDK_TYPE_TEXTURE;
        g_paste_clipboard_meta_formats[1].type = GDK_TYPE_RGBA;
        g_paste_clipboard_meta_formats[2].type = GDK_TYPE_FILE_LIST;

        for (guint i = 0; i < G_N_ELEMENTS (g_paste_clipboard_meta_formats); ++i)
        {
            GType type = g_paste_clipboard_meta_formats[i].type;
            const gchar *mimetype = g_paste_clipboard_meta_formats[i].mimetype;
            g_autoptr (GdkContentFormats) known = gdk_content_formats_union_deserialize_mime_types (gdk_content_formats_new_for_gtype (type));

            if (gdk_content_formats_contain_mime_type (known, mimetype))
                continue;

            gdk_content_register_deserializer (mimetype, type, g_paste_clipboard_meta_deserialize,
                                               (gpointer) g_paste_clipboard_meta_formats[i].decode, NULL);
            gdk_content_register_serializer (type, mimetype, g_paste_clipboard_meta_serialize, NULL, NULL);
            g_paste_clipboard_meta_formats[i].registered = TRUE;
        }
        g_once_init_leave (&registered, 1);
    }
}

/* --- GDK-backed format negotiation --- */

/*
 * GDK never names media formats: it negotiates through a #GType (GDK_TYPE_TEXTURE
 * for images, GDK_TYPE_RGBA for colors, GDK_TYPE_FILE_LIST for files) and lets
 * its registered (de)serializers expand that to the mimetypes they handle. That
 * set is what the mutter backend advertises (on write) and accepts (on read):
 * GDK's own where GTK registered them, the mimetypes the GDK backend takes too,
 * and in gnome-shell, where it registers none, the one format per type
 * g_paste_clipboard_meta_ensure_formats () gives.
 *
 * The daemon is single-threaded (GTK main loop), so a plain #GType -> formats
 * cache needs no extra locking beyond creating it once.
 */
static GHashTable *g_paste_clipboard_meta_deserialize_formats_cache = NULL;

static void
g_paste_clipboard_meta_clear_deserialize_formats_cache (void)
{
    g_clear_pointer (&g_paste_clipboard_meta_deserialize_formats_cache, g_hash_table_destroy);
}

static GdkContentFormats *
g_paste_clipboard_meta_deserialize_formats (GType type)
{
    if (g_once_init_enter_pointer (&g_paste_clipboard_meta_deserialize_formats_cache))
    {
        /* The cached formats are owned by the table (one ref each); free them
         * along with the process-wide cache at exit. */
        GHashTable *cache = g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify) gdk_content_formats_unref);

        atexit (g_paste_clipboard_meta_clear_deserialize_formats_cache);
        g_once_init_leave_pointer (&g_paste_clipboard_meta_deserialize_formats_cache, cache);
    }

    GHashTable *cache = g_paste_clipboard_meta_deserialize_formats_cache;
    GdkContentFormats *formats = g_hash_table_lookup (cache, GSIZE_TO_POINTER (type));

    if (!formats)
    {
        formats = gdk_content_formats_union_deserialize_mime_types (gdk_content_formats_new_for_gtype (type));
        g_hash_table_insert (cache, GSIZE_TO_POINTER (type), formats);
    }

    return formats;
}

/* --- a multi-mimetype MetaSelectionSource --- */

/*
 * mutter only ships #MetaSelectionSourceMemory, which serves a single mimetype.
 * To advertise several representations of the same payload (plain text plus its
 * HTML/XML special values, or text/uri-list plus x-special/gnome-copied-files)
 * we derive our own source holding an ordered list of mimetypes and a
 * mimetype -> #GBytes map. Textures, colors and file lists are special: rather
 * than store one rendered payload, we keep the typed #GValue and let GDK
 * serialise it to whichever format a reader asks for, on demand.
 */

#define G_PASTE_TYPE_CLIPBOARD_META_SOURCE (g_paste_clipboard_meta_source_get_type ())

G_DECLARE_FINAL_TYPE (GPasteClipboardMetaSource, g_paste_clipboard_meta_source, G_PASTE, CLIPBOARD_META_SOURCE, MetaSelectionSource)

struct _GPasteClipboardMetaSource
{
    MetaSelectionSource parent_instance;

    GList              *mimetypes;           /* owned list of gchar*, in advertised order */
    GHashTable         *contents;            /* gchar* mimetype -> GBytes */
    GValue              value;               /* G_VALUE_INIT unless a typed payload is bridged */
    GHashTable         *serialize_mimetypes; /* gchar* set of mimetypes served from @value */
};

G_DEFINE_FINAL_TYPE (GPasteClipboardMetaSource, g_paste_clipboard_meta_source, META_TYPE_SELECTION_SOURCE)

static GList *
g_paste_clipboard_meta_source_get_mimetypes (MetaSelectionSource *source)
{
    const GPasteClipboardMetaSource *self = G_PASTE_CLIPBOARD_META_SOURCE (source);
    GList *ret = NULL;

    for (const GList *m = self->mimetypes; m; m = m->next)
        ret = g_list_prepend (ret, g_strdup (m->data));

    return g_list_reverse (ret);
}

typedef struct
{
    GTask         *task;
    GOutputStream *ostream;
} GPasteClipboardMetaSourceSerializeData;

static void
g_paste_clipboard_meta_source_on_serialized (GObject      *source_object G_GNUC_UNUSED,
                                             GAsyncResult *res,
                                             gpointer      user_data)
{
    g_autofree GPasteClipboardMetaSourceSerializeData *data = user_data;
    g_autoptr (GTask) task = data->task;
    g_autoptr (GOutputStream) ostream = data->ostream;
    g_autoptr (GError) error = NULL;

    if (!gdk_content_serialize_finish (res, &error))
    {
        g_task_return_error (task, g_steal_pointer (&error));
        return;
    }

    /* steal_as_bytes requires a closed stream and GDK leaves it open. Closing a
     * #GMemoryOutputStream cannot fail, hence the unchecked error. */
    g_output_stream_close (ostream, NULL, NULL);

    g_autoptr (GBytes) bytes = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (ostream));

    g_task_return_pointer (task, g_memory_input_stream_new_from_bytes (bytes), g_object_unref);
}

static void
g_paste_clipboard_meta_source_read_async (MetaSelectionSource *source,
                                          const gchar         *mimetype,
                                          GCancellable        *cancellable,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data)
{
    const GPasteClipboardMetaSource *self = G_PASTE_CLIPBOARD_META_SOURCE (source);
    GTask *task = g_task_new (source, cancellable, callback, user_data);

    g_task_set_source_tag (task, g_paste_clipboard_meta_source_read_async);

    /* Disposed, which only an explicit g_object_run_dispose () does: a read in
     * flight holds the source, and mutter swapping the owner out only drops its
     * own reference. Everything a read is served from went with dispose, so this
     * answers that there is nothing left rather than look in tables that are no
     * longer there. */
    if (!self->contents)
    {
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CLOSED,
                                 "The selection source is gone");
        g_object_unref (task);
        return;
    }

    GBytes *bytes = g_hash_table_lookup (self->contents, mimetype);

    if (bytes)
    {
        g_task_return_pointer (task, g_memory_input_stream_new_from_bytes (bytes), g_object_unref);
        g_object_unref (task);
        return;
    }

    if (g_hash_table_contains (self->serialize_mimetypes, mimetype))
    {
        GPasteClipboardMetaSourceSerializeData *data = g_new0 (GPasteClipboardMetaSourceSerializeData, 1);

        data->task = task;
        data->ostream = g_memory_output_stream_new_resizable ();

        /* Let GDK render the typed payload into the requested format on demand. */
        gdk_content_serialize_async (data->ostream,
                                     mimetype,
                                     &self->value,
                                     G_PRIORITY_DEFAULT,
                                     cancellable,
                                     g_paste_clipboard_meta_source_on_serialized,
                                     data);
        return;
    }

    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                             "Mimetype '%s' is not available", mimetype);
    g_object_unref (task);
}

static GInputStream *
g_paste_clipboard_meta_source_read_finish (MetaSelectionSource *source,
                                           GAsyncResult        *result,
                                           GError             **error)
{
    g_return_val_if_fail (g_task_is_valid (result, source), NULL);

    return g_task_propagate_pointer (G_TASK (result), error);
}

static void
g_paste_clipboard_meta_source_dispose (GObject *object)
{
    GPasteClipboardMetaSource *self = G_PASTE_CLIPBOARD_META_SOURCE (object);

    g_clear_pointer (&self->contents, g_hash_table_unref);
    g_clear_pointer (&self->serialize_mimetypes, g_hash_table_unref);
    /* The bridged payload is a #GdkTexture or a #GdkFileList as often as it is a
     * plain string, so it is a reference like any other -- and unsetting zeroes
     * it, which is what makes a second dispose a no-op. */
    if (G_IS_VALUE (&self->value))
        g_value_unset (&self->value);
    /* Plain strings with nothing to unref, but dropped here with the rest: what
     * this list advertises is what the tables above answer for, so a disposed
     * source that still named its formats would be offering reads it can only
     * refuse. */
    g_clear_list (&self->mimetypes, g_free);

    G_OBJECT_CLASS (g_paste_clipboard_meta_source_parent_class)->dispose (object);
}

static void
g_paste_clipboard_meta_source_class_init (GPasteClipboardMetaSourceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MetaSelectionSourceClass *source_class = META_SELECTION_SOURCE_CLASS (klass);

    object_class->dispose = g_paste_clipboard_meta_source_dispose;
    source_class->get_mimetypes = g_paste_clipboard_meta_source_get_mimetypes;
    source_class->read_async = g_paste_clipboard_meta_source_read_async;
    source_class->read_finish = g_paste_clipboard_meta_source_read_finish;
}

static void
g_paste_clipboard_meta_source_init (GPasteClipboardMetaSource *self)
{
    self->contents = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_bytes_unref);
    self->serialize_mimetypes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
g_paste_clipboard_meta_source_add (GPasteClipboardMetaSource *self,
                                   const gchar               *mimetype,
                                   GBytes                    *bytes)
{
    /* Keep the first representation registered for a given mimetype. */
    if (g_hash_table_contains (self->contents, mimetype))
        return;

    self->mimetypes = g_list_append (self->mimetypes, g_strdup (mimetype));
    g_hash_table_insert (self->contents, g_strdup (mimetype), g_bytes_ref (bytes));
}

/* Advertise plain text under both the utf-8 and the bare text/plain mimetype, as
 * a #GdkContentProvider for G_TYPE_STRING would. */
static void
g_paste_clipboard_meta_source_add_text (GPasteClipboardMetaSource *self,
                                        GBytes                    *bytes)
{
    g_paste_clipboard_meta_source_add (self, META_MIME_TEXT, bytes);
    g_paste_clipboard_meta_source_add (self, META_MIME_TEXT_PLAIN, bytes);
}

/* Hold a typed payload (texture/color/file-list) and advertise every format GDK
 * can serialise it into; the bytes are produced lazily in read_async. Matches
 * gdk_clipboard_set (.., G_VALUE_TYPE (value), ..). */
static void
g_paste_clipboard_meta_source_add_value (GPasteClipboardMetaSource *self,
                                         const GValue              *value)
{
    g_autoptr (GdkContentFormats) formats = gdk_content_formats_union_serialize_mime_types (gdk_content_formats_new_for_gtype (G_VALUE_TYPE (value)));
    gsize n_mimetypes = 0;
    const gchar * const *mimetypes = gdk_content_formats_get_mime_types (formats, &n_mimetypes);

    g_value_init (&self->value, G_VALUE_TYPE (value));
    g_value_copy (value, &self->value);

    for (gsize i = 0; i < n_mimetypes; ++i)
    {
        if (g_hash_table_contains (self->contents, mimetypes[i]) ||
            !g_hash_table_add (self->serialize_mimetypes, g_strdup (mimetypes[i])))
            continue;

        self->mimetypes = g_list_append (self->mimetypes, g_strdup (mimetypes[i]));
    }
}

/* Append every rich-text/HTML/XML/gnome-copied-files representation an item
 * carries, mirroring g_paste_clipboard_gdk_select_item. */
static void
g_paste_clipboard_meta_source_add_special_values (GPasteClipboardMetaSource *self,
                                                  GPasteItem                *item)
{
    for (const GSList *sv = g_paste_item_get_special_values (item); sv; sv = sv->next)
    {
        GPasteBinaryData *v = sv->data;

        g_paste_clipboard_meta_source_add (self,
                                           g_paste_special_mime_get (g_paste_binary_data_get_mime (v)),
                                           g_paste_binary_data_get_bytes (v));
    }
}

/* Say that what this source offers is a secret, mirroring
 * g_paste_clipboard_gdk_select_item (). */
static void
g_paste_clipboard_meta_source_add_sensitive_mimes (GPasteClipboardMetaSource *self,
                                                   GPasteItem                *item)
{
    if (!G_PASTE_IS_PASSWORD_ITEM (item))
        return;

    for (GPasteSensitiveMime mime = G_PASTE_SENSITIVE_MIME_FIRST; mime < G_PASTE_SENSITIVE_MIME_LAST; ++mime)
        g_paste_clipboard_meta_source_add (self, g_paste_sensitive_mime_get (mime), g_paste_sensitive_mime_get_bytes (mime));
}

/* --- publishing a source --- */

static void
g_paste_clipboard_meta_publish_source (GPasteClipboardMeta       *self,
                                       GPasteClipboardMetaSource *source)
{
    g_paste_clipboard_update_supersede (&self->update);

    /* Mutter emits owner-changed synchronously; owned_source makes that
     * notification a no-op here. Its set_owner does not cancel transfers;
     * superseded read completion is deferred by the shared update helper. */
    g_set_object (&self->owned_source, META_SELECTION_SOURCE (source));
    meta_selection_set_owner (self->selection, self->type, META_SELECTION_SOURCE (source));
    g_object_unref (source);
}

/* --- select_text --- */

static void
g_paste_clipboard_meta_private_set_text (GPasteClipboardMeta *self,
                                         const gchar         *text)
{
    g_debug ("%s: set text", g_paste_clipboard_provider_target_name (self->is_clipboard));

    g_paste_clipboard_content_set_text (&self->content, text);
}

/* mutter refuses no owner, so a publication here is always one. */
static GPasteClipboardPublication
g_paste_clipboard_meta_select_text (GPasteClipboardMeta *self,
                                    const gchar         *text)
{
    g_debug ("%s: select text", g_paste_clipboard_provider_target_name (self->is_clipboard));

    g_paste_clipboard_meta_private_set_text (self, text);

    g_autoptr (GBytes) bytes = g_bytes_new (text, strlen (text));
    GPasteClipboardMetaSource *source = g_object_new (G_PASTE_TYPE_CLIPBOARD_META_SOURCE, NULL);

    g_paste_clipboard_meta_source_add_text (source, bytes);
    g_paste_clipboard_meta_publish_source (self, source);

    return G_PASTE_CLIPBOARD_PUBLICATION_PUBLISHED;
}

/* --- sync_text --- */

static void
g_paste_clipboard_meta_sync_ready (GPasteClipboardMeta *self G_GNUC_UNUSED,
                                   GBytes              *bytes,
                                   gpointer             user_data)
{
    g_autoptr (GPasteClipboardSyncData) data = user_data; /* built in sync_text */

    /* Asked before the copy below and not only by deliver (): the bytes this read
     * landed with are the selection's whole text, and duplicating them for a sync
     * that has nowhere left to put them is the one read that is certainly wasted.
     * The GDK backend asks it before its own copy of a bare text/plain; its
     * other reads hand over a text GDK already decoded. */
    if (!g_paste_clipboard_sync_data_wants_text (data))
        return;

    /* An empty transfer is an empty text, as update_on_text () takes it: the
     * other selection gets the empty string rather than keeping what it had. */
    g_autofree gchar *text = g_paste_clipboard_text_from_bytes (bytes, NULL);

    if (text)
        g_paste_clipboard_sync_data_deliver (data, text);
}

static void
g_paste_clipboard_meta_sync_text (GPasteClipboardMeta        *self,
                                  GPasteClipboardMeta        *other,
                                  GPasteClipboardSyncCallback callback,
                                  gpointer                    user_data,
                                  GDestroyNotify              destroy)
{
    GList *mimetypes = meta_selection_get_mimetypes (self->selection, self->type);
    /* Prefer the utf-8 form but accept bare text/plain too, like update() does, so a
     * source that only advertises text/plain still syncs (both are static strings,
     * safe to hand to the async read after the list is freed). */
    const gchar *mime = mimetypes_contain (mimetypes, META_MIME_TEXT) ? META_MIME_TEXT
                      : mimetypes_contain (mimetypes, META_MIME_TEXT_PLAIN) ? META_MIME_TEXT_PLAIN
                      : NULL;

    if (mime)
    {
        GPasteClipboardSyncData *data = g_paste_clipboard_sync_data_new (G_PASTE_CLIPBOARD_PROVIDER (other),
                                                                         callback, user_data, destroy);

        if (data)
        {
            g_paste_clipboard_meta_read_mime (self, mime, data->guard.cancellable, g_paste_clipboard_meta_sync_ready, data);
            destroy = NULL;
        }
    }

    /* No read to hand it to, so nothing is going to be published and nothing
     * will free what the caller handed over: that is this call's to do. */
    if (destroy)
        destroy (user_data);

    g_list_free_full (mimetypes, g_free);
}

/* --- store --- */

static void
g_paste_clipboard_meta_store (GPasteClipboardMeta *self G_GNUC_UNUSED)
{
    /* mutter owns the selection for the whole session; there is nothing to hand
     * off to a clipboard manager on exit the way GdkClipboard::store does. */
    g_debug ("meta clipboard: store (no-op)");
}

/* --- select_item --- */

static GPasteClipboardPublication
g_paste_clipboard_meta_select_item (GPasteClipboardMeta *self,
                                    GPasteItem          *item)
{
    g_debug ("%s: select item", g_paste_clipboard_provider_target_name (self->is_clipboard));

    if (G_PASTE_IS_IMAGE_ITEM (item))
    {
        GdkTexture *texture = g_paste_image_item_get_image (G_PASTE_IMAGE_ITEM (item));
        const gchar *checksum = g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (item));

        if (!texture)
            return G_PASTE_CLIPBOARD_PUBLICATION_INVALID;

        g_paste_clipboard_content_set_image_checksum (&self->content, checksum);

        g_auto (GValue) value = G_VALUE_INIT;
        GPasteClipboardMetaSource *source = g_object_new (G_PASTE_TYPE_CLIPBOARD_META_SOURCE, NULL);

        g_value_init (&value, GDK_TYPE_TEXTURE);
        g_value_set_object (&value, texture);
        g_paste_clipboard_meta_source_add_value (source, &value);
        g_paste_clipboard_meta_publish_source (self, source);
        return G_PASTE_CLIPBOARD_PUBLICATION_PUBLISHED;
    }

    if (G_PASTE_IS_COLOR_ITEM (item))
    {
        const GdkRGBA *rgba = g_paste_color_item_get_rgba (G_PASTE_COLOR_ITEM (item));

        g_paste_clipboard_content_set_color (&self->content, rgba);

        g_auto (GValue) value = G_VALUE_INIT;
        GPasteClipboardMetaSource *source = g_object_new (G_PASTE_TYPE_CLIPBOARD_META_SOURCE, NULL);

        /* Let GDK encode application/x-color, byte-identical to the GDK backend. */
        g_value_init (&value, GDK_TYPE_RGBA);
        g_value_set_boxed (&value, rgba);
        g_paste_clipboard_meta_source_add_value (source, &value);

        /* Plus the textual form, so the colour pastes into plain text fields too. */
        const gchar *real_value = g_paste_item_get_real_value (item);
        g_autoptr (GBytes) text_bytes = g_bytes_new (real_value, strlen (real_value));

        g_paste_clipboard_meta_source_add_text (source, text_bytes);
        g_paste_clipboard_meta_publish_source (self, source);
        return G_PASTE_CLIPBOARD_PUBLICATION_PUBLISHED;
    }

    if (G_PASTE_IS_URIS_ITEM (item))
    {
        GdkFileList *file_list = g_paste_uris_item_get_file_list (G_PASTE_URIS_ITEM (item));

        g_paste_clipboard_content_set_file_list (&self->content, file_list);

        g_auto (GValue) value = G_VALUE_INIT;
        GPasteClipboardMetaSource *source = g_object_new (G_PASTE_TYPE_CLIPBOARD_META_SOURCE, NULL);

        /* GDK serialises the list to whatever it knows for files --
         * in gnome-shell, text/uri-list alone (see
         * g_paste_clipboard_meta_ensure_formats ());
         * x-special/gnome-copied-files rides as a special value. */
        g_value_init (&value, GDK_TYPE_FILE_LIST);
        g_value_set_boxed (&value, file_list);
        g_paste_clipboard_meta_source_add_value (source, &value);
        g_paste_clipboard_meta_source_add_special_values (source, item);
        g_paste_clipboard_meta_publish_source (self, source);
        return G_PASTE_CLIPBOARD_PUBLICATION_PUBLISHED;
    }

    /* Plain text, with any rich-text/HTML/XML special values offered alongside. */
    const gchar *real_value = g_paste_item_get_real_value (item);
    g_paste_clipboard_meta_private_set_text (self, real_value);

    g_autoptr (GBytes) bytes = g_bytes_new (real_value, strlen (real_value));
    GPasteClipboardMetaSource *source = g_object_new (G_PASTE_TYPE_CLIPBOARD_META_SOURCE, NULL);

    g_paste_clipboard_meta_source_add_text (source, bytes);
    g_paste_clipboard_meta_source_add_special_values (source, item);
    g_paste_clipboard_meta_source_add_sensitive_mimes (source, item);
    g_paste_clipboard_meta_publish_source (self, source);

    return G_PASTE_CLIPBOARD_PUBLICATION_PUBLISHED;
}

static gboolean
g_paste_clipboard_meta_is_empty (GPasteClipboardMeta *self)
{
    return g_paste_clipboard_content_is_empty (&self->content);
}

/* --- update --- */

/* Every read below counts into a #GPasteClipboardUpdate, as the GDK backend's do:
 * only the calls that fetch the bytes are this backend's -- one transfer per
 * mimetype, the content deserialised afterwards from the mimetype the update
 * carries. */
static void
g_paste_clipboard_meta_update_on_text (GPasteClipboardMeta *self G_GNUC_UNUSED,
                                       GBytes              *bytes,
                                       gpointer             user_data)
{
    GPasteClipboardUpdate *update = user_data;

    if (g_paste_clipboard_update_is_expired (update) || !bytes)
    {
        g_paste_clipboard_update_maybe_done (update);
        return;
    }

    /* An empty transfer comes back as bytes with no data at all, the stream it
     * was written to having never grown: that is an empty text, not a read that
     * failed, and it is what a password manager clearing its copy writes. It is
     * handed over as the empty string, which conclude () takes for the answer it
     * is -- where dropping it would leave the record of the password it replaced
     * unconfirmed for good (see the DROP case there).
     *
     * A source that closes its end without writing anything -- one that failed
     * to serialize, or misbehaves -- comes back exactly the same: a pipe has no
     * way to tell the two apart. It is taken for an empty text all the same, and
     * the price is knowingly paid: such a source can have its password's record
     * retired over the cleartext it still holds, where the other answer leaves
     * every correct password manager's clear unconfirmed for good -- a record
     * that refuses every sync out of that selection, and, while it runs a
     * countdown, fails every re-exec and storage migration waiting on expiry.
     *
     * A text that is not UTF-8 is no text this backend can read. */
    g_autofree gchar *text = g_paste_clipboard_text_from_bytes (bytes, NULL);

    if (!text)
    {
        g_paste_clipboard_update_maybe_done (update);
        return;
    }

    /* Kept whatever its size, and classified in
     * g_paste_clipboard_update_conclude (): the size policy exempts a secret, and
     * whether this is one is what the hint reads still out beside this one
     * answer, so nothing may turn a text down before they land. What a read with
     * nowhere left to land is turned down on is the deadline above, as
     * g_paste_clipboard_sync_data_wants_text () turns the sync beside it down.
     *
     * Copied rather than kept as @bytes, which would hold it no shorter: that
     * buffer is the transfer's resizable stream, grown by doubling and handed
     * over whole, so it can be near twice the text it carries. The string is the
     * text's own length, and the buffer goes back as this returns. */
    update->produced = TRUE;
    g_set_str_take (&update->text, g_steal_pointer (&text));
    g_paste_clipboard_update_maybe_done (update);
}

/* The GType GDK deserialises each non-text content kind into. */
static GType
g_paste_clipboard_meta_content_gtype (GPasteClipboardContentKind content_kind)
{
    switch (content_kind)
    {
    case CLIPBOARD_CONTENT_IMAGE:
        return GDK_TYPE_TEXTURE;
    case CLIPBOARD_CONTENT_COLOR:
        return GDK_TYPE_RGBA;
    case CLIPBOARD_CONTENT_FILE_LIST:
        return GDK_TYPE_FILE_LIST;
    default:
        return G_TYPE_INVALID;
    }
}

/* What a value read decoded to, @value, or what kept it from decoding, @error. */
static void
g_paste_clipboard_meta_update_on_value_decoded (GPasteClipboardUpdate *update,
                                                const GValue          *value,
                                                const GError          *error)
{
    GPasteClipboardMeta *self = G_PASTE_CLIPBOARD_META (update->provider);

    if (error)
    {
        g_debug ("Failed to decode selection: %s", error->message);
        g_paste_clipboard_update_maybe_done (update);
        return;
    }

    switch (update->content_kind)
    {
    case CLIPBOARD_CONTENT_IMAGE:
    {
        g_autoptr (GdkTexture) texture = g_value_dup_object (value);

        if (!texture)
            break;

        g_autofree gchar *checksum = g_paste_image_item_compute_checksum (texture);

        if (self->content.kind == CLIPBOARD_CONTENT_IMAGE && g_paste_str_equal (checksum, self->content.str))
        {
            update->unchanged = TRUE;
            break;
        }

        update->produced = TRUE;
        update->texture = g_steal_pointer (&texture);
        break;
    }
    case CLIPBOARD_CONTENT_COLOR:
    {
        const GdkRGBA *rgba = g_value_get_boxed (value);

        if (!rgba)
            break;

        if (self->content.kind == CLIPBOARD_CONTENT_COLOR && gdk_rgba_equal (rgba, &self->content.rgba))
        {
            update->unchanged = TRUE;
            break;
        }

        update->produced = TRUE;
        update->rgba = *rgba;
        break;
    }
    case CLIPBOARD_CONTENT_FILE_LIST:
    {
        GdkFileList *file_list = g_value_get_boxed (value);
        /* (transfer container): the container is ours, the GFiles are not. */
        g_autoptr (GSList) files = (file_list) ? gdk_file_list_get_files (file_list) : NULL;

        if (!files)
            break;

        /* Re-asserting the same file selection must not re-add it, mirroring the
         * GDK backend's read-path g_paste_clipboard_file_list_equal guard. */
        if (g_paste_clipboard_file_list_equal (g_paste_clipboard_content_get_file_list (&self->content), file_list))
        {
            update->unchanged = TRUE;
            break;
        }

        update->produced = TRUE;
        update->file_list = g_boxed_copy (GDK_TYPE_FILE_LIST, file_list);
        break;
    }
    default:
        break;
    }

    g_paste_clipboard_update_maybe_done (update);
}

static void
g_paste_clipboard_meta_update_on_value_deserialized (GObject      *source_object G_GNUC_UNUSED,
                                                     GAsyncResult *res,
                                                     gpointer      user_data)
{
    GPasteClipboardUpdate *update = user_data;
    g_auto (GValue) value = G_VALUE_INIT;
    g_autoptr (GError) error = NULL;

    /* Superseded or timed out, the cache is the only place this could reach: see
     * g_paste_clipboard_update_is_expired (). Asked before the provider is read
     * off @update, a concluded one having handed it on. */
    if (g_paste_clipboard_update_is_expired (update))
    {
        g_paste_clipboard_update_maybe_done (update);
        return;
    }

    g_value_init (&value, g_paste_clipboard_meta_content_gtype (update->content_kind));
    if (!gdk_content_deserialize_finish (res, &value, &error) && !error)
        g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_FAILED, "Nothing decoded");
    g_paste_clipboard_meta_update_on_value_decoded (update, &value, error);
}

/* image/color/file-list all decode through GDK's deserialisers, whichever are
 * registered for the type: see g_paste_clipboard_meta_deserialize_formats (). */
static void
g_paste_clipboard_meta_update_on_value (GPasteClipboardMeta *self G_GNUC_UNUSED,
                                        GBytes              *bytes,
                                        gpointer             user_data)
{
    GPasteClipboardUpdate *update = user_data;

    /* Superseded or timed out, the cache is the only place this could reach.
     * Asked before the deserialisation rather than only in its callback, there
     * being nothing left for it to deserialise for. */
    if (g_paste_clipboard_update_is_expired (update) || !bytes)
    {
        g_paste_clipboard_update_maybe_done (update);
        return;
    }

    /* The bytes are in, which is this batch demonstrably moving -- and the one
     * read that does not report it by counting itself out, the slot it holds
     * being chained into the deserialisation below rather than released. Without
     * this, that deserialisation runs on whatever was left of the deadline when
     * the transfer started. */
    g_paste_clipboard_read_guard_touch (&update->guard);

    /* One of the formats this file gave GDK, decoded straight off the bytes the
     * transfer already holds, rather than streamed into a second copy of them by
     * the deserialiser GDK would run for it. No test tells one copy from two. */
    GPasteClipboardMetaDecode decode = g_paste_clipboard_meta_own_decode (update->mime);

    if (decode)
    {
        g_auto (GValue) value = G_VALUE_INIT;
        g_autoptr (GError) error = NULL;

        g_value_init (&value, g_paste_clipboard_meta_content_gtype (update->content_kind));
        decode (&value, bytes, &error);
        g_paste_clipboard_meta_update_on_value_decoded (update, &value, error);
        return;
    }

    g_autoptr (GInputStream) stream = g_memory_input_stream_new_from_bytes (bytes);

    gdk_content_deserialize_async (stream,
                                   update->mime,
                                   g_paste_clipboard_meta_content_gtype (update->content_kind),
                                   G_PRIORITY_DEFAULT,
                                   update->guard.cancellable,
                                   g_paste_clipboard_meta_update_on_value_deserialized,
                                   update);
}

/* As in g_paste_clipboard_gdk_on_mime_read (): the backend only adapts the
 * callback's shape; the shared update interprets and counts the MIME reply. */
static void
g_paste_clipboard_meta_on_mime_read (GPasteClipboardMeta *self G_GNUC_UNUSED,
                                     GBytes              *bytes,
                                     gpointer             user_data)
{
    g_paste_clipboard_update_on_mime_read (user_data, bytes);
}

/* The two halves this backend answers g_paste_clipboard_update_read_mimes ()
 * with: what a selection offers is the mimetype list the owner published, and a
 * read of one mimetype is a transfer reporting to on_mime_read (). Which
 * mimetypes are asked for is that function's, not this file's. */
static gboolean
g_paste_clipboard_meta_offers_mime (gconstpointer offer,
                                    const gchar  *mimetype)
{
    return mimetypes_contain ((GList *) offer, mimetype);
}

static void
g_paste_clipboard_meta_read_offered_mime (gpointer                backend,
                                          const gchar            *mimetype,
                                          GCancellable           *cancellable,
                                          GPasteClipboardMimeCtx *ctx)
{
    g_paste_clipboard_meta_read_mime (backend, mimetype, cancellable, g_paste_clipboard_meta_on_mime_read, ctx);
}

/* Pick the offered mimetype to read @type from: @preferred (the canonical
 * representation) when it is on offer, otherwise the first format GDK can
 * deserialise into @type. Returns a pointer into @mimetypes, or NULL. */
static const gchar *
g_paste_clipboard_meta_pick_mime (GList       *mimetypes,
                                  GType        type,
                                  const gchar *preferred)
{
    GdkContentFormats *deserializable = g_paste_clipboard_meta_deserialize_formats (type);

    if (preferred && mimetypes_contain (mimetypes, preferred) &&
        gdk_content_formats_contain_mime_type (deserializable, preferred))
        return preferred;

    for (const GList *m = mimetypes; m; m = m->next)
    {
        if (gdk_content_formats_contain_mime_type (deserializable, m->data))
            return m->data;
    }

    return NULL;
}

static void
g_paste_clipboard_meta_update (GPasteClipboardMeta                  *self,
                               gboolean                              record,
                               GPasteClipboardProviderUpdateCallback callback,
                               gpointer                              user_data)
{
    GList *mimetypes = meta_selection_get_mimetypes (self->selection, self->type);
    GPasteClipboardContentKind content_kind = CLIPBOARD_CONTENT_NONE;
    const gchar *content_mime = NULL;

    if ((content_mime = g_paste_clipboard_meta_pick_mime (mimetypes, GDK_TYPE_FILE_LIST, META_MIME_URIS)))
    {
        content_kind = CLIPBOARD_CONTENT_FILE_LIST;
    }
    else if ((content_mime = g_paste_clipboard_meta_pick_mime (mimetypes, GDK_TYPE_RGBA, NULL)))
    {
        content_kind = CLIPBOARD_CONTENT_COLOR;
    }
    /* Text before an image, as in the GDK backend's update (). */
    else if (mimetypes_contain (mimetypes, META_MIME_TEXT) ||
             mimetypes_contain (mimetypes, META_MIME_TEXT_PLAIN))
    {
        content_kind = CLIPBOARD_CONTENT_TEXT;
        content_mime = mimetypes_contain (mimetypes, META_MIME_TEXT) ? META_MIME_TEXT : META_MIME_TEXT_PLAIN;
    }
    else if (g_paste_settings_get_images_support (self->settings) &&
             (content_mime = g_paste_clipboard_meta_pick_mime (mimetypes, GDK_TYPE_TEXTURE, META_MIME_IMAGE)))
    {
        content_kind = CLIPBOARD_CONTENT_IMAGE;
    }
    else if (!mimetypes)
    {
        /* The selection was released: clear our cache so callers see an
         * empty clipboard and act accordingly (e.g. ensure_not_empty).
         * See g_paste_clipboard_update_supersede () for changes with no read. */
        g_paste_clipboard_update_supersede (&self->update);
        g_paste_clipboard_content_clear (&self->content);
        if (callback)
            callback (G_PASTE_CLIPBOARD_PROVIDER (self), NULL, FALSE, CLIPBOARD_SECRET_NO, user_data);
        return;
    }
    else
    {
        /* The owner only provides types we don't handle (e.g. an image
         * while images-support is disabled). Don't track it, but flag the
         * clipboard as non-empty so ensure_not_empty doesn't override it --
         * and give up on the update the previous owner was being read for. */
        g_list_free_full (mimetypes, g_free);
        g_paste_clipboard_update_supersede (&self->update);
        g_paste_clipboard_content_set_ignored (&self->content);
        if (callback)
            callback (G_PASTE_CLIPBOARD_PROVIDER (self), NULL, FALSE, CLIPBOARD_SECRET_NO, user_data);
        return;
    }

    GPasteClipboardUpdate *update = g_paste_clipboard_update_new (G_PASTE_CLIPBOARD_PROVIDER (self),
                                                                  self->settings,
                                                                  content_kind,
                                                                  &self->update,
                                                                  &self->content,
                                                                  callback,
                                                                  user_data);

    /* As in the GDK backend's update (): the one path that fires no read still
     * answers. Those preconditions are the provider's own state, out of a
     * library test's reach; the GDK backend's twin is pinned
     * (/gdk/update/unmade). */
    if (!update)
    {
        g_list_free_full (mimetypes, g_free);
        if (callback)
            callback (G_PASTE_CLIPBOARD_PROVIDER (self), NULL, FALSE, CLIPBOARD_SECRET_UNKNOWN, user_data);
        return;
    }

    update->record = record;

    /* What the content read is fired with, whatever its kind, and never
     * content_mime itself: that one aliases the mimetypes list freed below,
     * while the transfer reads the string after the call returns. The update
     * holds this copy until every read it fired has reported, which is what
     * makes it outlive them (g_paste_clipboard_update_maybe_done ()) -- and the
     * value reads need it a second time anyway, for the deserialisation once the
     * bytes have arrived. */
    update->mime = g_strdup (content_mime);

    /* Counted in beside the read it counts, never before the switch: an arm that
     * fires nothing would leave the update pending on a read that does not
     * exist. */
    switch (content_kind)
    {
    case CLIPBOARD_CONTENT_FILE_LIST:
    case CLIPBOARD_CONTENT_COLOR:
    case CLIPBOARD_CONTENT_IMAGE:
        g_paste_clipboard_update_add_read (update);
        g_paste_clipboard_meta_read_mime (self, update->mime, update->guard.cancellable, g_paste_clipboard_meta_update_on_value, update);
        break;
    case CLIPBOARD_CONTENT_TEXT:
        g_paste_clipboard_update_add_read (update);
        g_paste_clipboard_meta_read_mime (self, update->mime, update->guard.cancellable, g_paste_clipboard_meta_update_on_text, update);
        break;
    case CLIPBOARD_CONTENT_IGNORED:
    case CLIPBOARD_CONTENT_WITHHELD:
    case CLIPBOARD_CONTENT_NONE:
        g_assert_not_reached ();
    }

    /* Every representation asked for on top of the content, the mimetypes the
     * owner listed saying which are there to ask for. */
    g_paste_clipboard_update_read_mimes (update, mimetypes, g_paste_clipboard_meta_offers_mime,
                                         self, g_paste_clipboard_meta_read_offered_mime);

    g_list_free_full (mimetypes, g_free);

    g_paste_clipboard_update_maybe_done (update);
}

/* --- external ownership change --- */

static void
g_paste_clipboard_meta_on_owner_changed (GPasteClipboardMeta *self,
                                         guint                selection_type,
                                         MetaSelectionSource *source)
{
    if ((MetaSelectionType) selection_type != self->type)
        return;

    /* Our own writes come back here too: skip them, just like the GDK backend
     * skips gdk_clipboard_is_local() changes. */
    if (source && source == self->owned_source)
        return;

    g_debug ("%s: owner change", g_paste_clipboard_provider_target_name (self->is_clipboard));
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (self));
}

/* GPasteClipboardProvider interface adapters */
G_PASTE_CLIPBOARD_PROVIDER_DEFINE_VFUNCS (meta, META)

static void
g_paste_clipboard_meta_dispose (GObject *object)
{
    GPasteClipboardMeta *self = G_PASTE_CLIPBOARD_META (object);

    /* g_clear_signal_handler () no-ops on an id of 0 and zeroes the one it
     * disconnects, so the instance is the only thing left to check for. */
    if (self->selection)
        g_clear_signal_handler (&self->owner_changed_id, self->selection);
    g_clear_object (&self->owned_source);
    g_clear_object (&self->selection);
    g_clear_object (&self->settings);

    G_OBJECT_CLASS (g_paste_clipboard_meta_parent_class)->dispose (object);
}

static void
g_paste_clipboard_meta_finalize (GObject *object)
{
    GPasteClipboardMeta *self = G_PASTE_CLIPBOARD_META (object);

    g_paste_clipboard_content_clear (&self->content);

    G_OBJECT_CLASS (g_paste_clipboard_meta_parent_class)->finalize (object);
}

static void
g_paste_clipboard_meta_class_init (GPasteClipboardMetaClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_clipboard_meta_dispose;
    object_class->finalize = g_paste_clipboard_meta_finalize;
}

static void
g_paste_clipboard_meta_init (GPasteClipboardMeta *self G_GNUC_UNUSED)
{
}

static GPasteClipboardProvider *
_g_paste_clipboard_meta_new (MetaSelection  *selection,
                             GPasteSettings *settings,
                             gboolean        is_clipboard)
{
    g_paste_clipboard_meta_ensure_formats ();

    GPasteClipboardMeta *self = g_object_new (G_PASTE_TYPE_CLIPBOARD_META, NULL);

    self->selection = g_object_ref (selection);
    self->type = is_clipboard ? META_SELECTION_CLIPBOARD : META_SELECTION_PRIMARY;
    self->is_clipboard = is_clipboard;
    self->settings = g_object_ref (settings);

    self->owner_changed_id = g_signal_connect_swapped (selection,
                                                       "owner-changed",
                                                       G_CALLBACK (g_paste_clipboard_meta_on_owner_changed),
                                                       self);

    return G_PASTE_CLIPBOARD_PROVIDER (self);
}

/**
 * g_paste_clipboard_meta_new_clipboard:
 * @selection: the #MetaSelection from global.display.get_selection()
 * @settings: a #GPasteSettings instance
 *
 * Create a new mutter-backed #GPasteClipboardProvider for the clipboard
 *
 * Returns: a newly allocated #GPasteClipboardProvider
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClipboardProvider *
g_paste_clipboard_meta_new_clipboard (MetaSelection  *selection,
                                      GPasteSettings *settings)
{
    g_return_val_if_fail (META_IS_SELECTION (selection), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    return _g_paste_clipboard_meta_new (selection, settings, TRUE);
}

/**
 * g_paste_clipboard_meta_new_primary:
 * @selection: the #MetaSelection from global.display.get_selection()
 * @settings: a #GPasteSettings instance
 *
 * Create a new mutter-backed #GPasteClipboardProvider for the primary selection
 *
 * Returns: a newly allocated #GPasteClipboardProvider
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClipboardProvider *
g_paste_clipboard_meta_new_primary (MetaSelection  *selection,
                                    GPasteSettings *settings)
{
    g_return_val_if_fail (META_IS_SELECTION (selection), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    return _g_paste_clipboard_meta_new (selection, settings, FALSE);
}
