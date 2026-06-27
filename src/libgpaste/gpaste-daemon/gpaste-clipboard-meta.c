// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <stdlib.h>
#include <string.h>

#include <gdk/gdk.h>

#include <gpaste-daemon/gpaste-binary-data.h>
#include <gpaste-daemon/gpaste-clipboard-content.h>
#include <gpaste-daemon/gpaste-clipboard-meta.h>
#include <gpaste-daemon/gpaste-color-item.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-special-atom.h>
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
 * The set of formats we actually accept/advertise comes from GDK, not these. */
#define META_MIME_IMAGE      "image/png"
#define META_MIME_URIS       "text/uri-list"

struct _GPasteClipboardMeta
{
    GObject parent_instance;
};

typedef struct
{
    MetaSelection         *selection;
    MetaSelectionType      type;
    gboolean               is_clipboard;
    GPasteSettings        *settings;
    MetaSelectionSource   *owned_source;
    gulong                 owner_changed_id;

    GPasteClipboardContent content;
} GPasteClipboardMetaPrivate;

static void g_paste_clipboard_meta_provider_iface_init (GPasteClipboardProviderInterface *iface);

G_PASTE_DEFINE_TYPE_WITH_PRIVATE_AND_INTERFACE (ClipboardMeta, clipboard_meta, G_TYPE_OBJECT,
                                                G_PASTE_TYPE_CLIPBOARD_PROVIDER, g_paste_clipboard_meta_provider_iface_init)

static gboolean
g_paste_clipboard_meta_is_clipboard (const GPasteClipboardMeta *self)
{
    const GPasteClipboardMetaPrivate *priv = _g_paste_clipboard_meta_get_instance_private (self);

    return priv->is_clipboard;
}

static const gchar *
g_paste_clipboard_meta_get_text (const GPasteClipboardMeta *self)
{
    const GPasteClipboardMetaPrivate *priv = _g_paste_clipboard_meta_get_instance_private (self);

    return g_paste_clipboard_content_get_text (&priv->content);
}

static const gchar *
g_paste_clipboard_meta_get_image_checksum (const GPasteClipboardMeta *self)
{
    const GPasteClipboardMetaPrivate *priv = _g_paste_clipboard_meta_get_instance_private (self);

    return g_paste_clipboard_content_get_image_checksum (&priv->content);
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

typedef struct {
    GPasteClipboardMeta             *self;
    GOutputStream                   *ostream;
    GPasteClipboardMetaBytesCallback callback;
    gpointer                         user_data;
} GPasteClipboardMetaReadData;

static void
g_paste_clipboard_meta_on_transfer_done (GObject      *source_object,
                                         GAsyncResult *res,
                                         gpointer      user_data)
{
    g_autofree GPasteClipboardMetaReadData *data = user_data;
    g_autoptr (GOutputStream) ostream = data->ostream;
    g_autoptr (GError) error = NULL;

    if (!meta_selection_transfer_finish (META_SELECTION (source_object), res, &error))
    {
        if (error)
            g_debug ("Failed to read selection content: %s", error->message);
        if (data->callback)
            data->callback (data->self, NULL, data->user_data);
        return;
    }

    g_autoptr (GBytes) bytes = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (ostream));

    if (data->callback)
        data->callback (data->self, bytes, data->user_data);
}

static void
g_paste_clipboard_meta_read_mime (GPasteClipboardMeta             *self,
                                  const gchar                     *mimetype,
                                  GPasteClipboardMetaBytesCallback callback,
                                  gpointer                         user_data)
{
    const GPasteClipboardMetaPrivate *priv = _g_paste_clipboard_meta_get_instance_private (self);
    GPasteClipboardMetaReadData *data = g_new0 (GPasteClipboardMetaReadData, 1);

    data->self = self;
    data->ostream = g_memory_output_stream_new_resizable ();
    data->callback = callback;
    data->user_data = user_data;

    meta_selection_transfer_async (priv->selection,
                                   priv->type,
                                   mimetype,
                                   -1, /* size unknown */
                                   data->ostream,
                                   NULL, /* cancellable */
                                   g_paste_clipboard_meta_on_transfer_done,
                                   data);
}

/* --- GDK-backed format negotiation --- */

/*
 * GDK never names media formats: it negotiates through a #GType (GDK_TYPE_TEXTURE
 * for images, GDK_TYPE_RGBA for colors, GDK_TYPE_FILE_LIST for files) and lets
 * its registered (de)serializers expand that to every installed representation.
 * Reuse those sets so the mutter backend advertises (on write) and accepts (on
 * read) exactly the mimetypes the GDK backend would, without hardcoding our own.
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

typedef struct {
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

    /* steal_as_bytes requires a closed stream and GDK leaves it open. */
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
    GBytes *bytes = g_hash_table_lookup (self->contents, mimetype);

    g_task_set_source_tag (task, g_paste_clipboard_meta_source_read_async);

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
g_paste_clipboard_meta_source_finalize (GObject *object)
{
    GPasteClipboardMetaSource *self = G_PASTE_CLIPBOARD_META_SOURCE (object);

    g_list_free_full (self->mimetypes, g_free);
    g_hash_table_unref (self->contents);
    g_hash_table_unref (self->serialize_mimetypes);
    if (G_IS_VALUE (&self->value))
        g_value_unset (&self->value);

    G_OBJECT_CLASS (g_paste_clipboard_meta_source_parent_class)->finalize (object);
}

static void
g_paste_clipboard_meta_source_class_init (GPasteClipboardMetaSourceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MetaSelectionSourceClass *source_class = META_SELECTION_SOURCE_CLASS (klass);

    object_class->finalize = g_paste_clipboard_meta_source_finalize;
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
                                                  const GPasteItem          *item)
{
    for (const GSList *sv = g_paste_item_get_special_values (item); sv; sv = sv->next)
    {
        const GPasteBinaryData *v = sv->data;

        g_paste_clipboard_meta_source_add (self,
                                           g_paste_special_atom_get (g_paste_binary_data_get_mime (v)),
                                           g_paste_binary_data_get_bytes (v));
    }
}

/* --- publishing a source --- */

static void
g_paste_clipboard_meta_publish_source (GPasteClipboardMeta       *self,
                                       GPasteClipboardMetaSource *source)
{
    GPasteClipboardMetaPrivate *priv = g_paste_clipboard_meta_get_instance_private (self);

    /* Keep our own ref so we can recognise the resulting owner-change as ours. */
    g_set_object (&priv->owned_source, META_SELECTION_SOURCE (source));
    meta_selection_set_owner (priv->selection, priv->type, META_SELECTION_SOURCE (source));
    g_object_unref (source);
}

/* --- select_text --- */

static void
g_paste_clipboard_meta_private_set_text (GPasteClipboardMetaPrivate *priv,
                                         const gchar                *text)
{
    g_debug ("%s: set text", g_paste_clipboard_provider_target_name (priv->is_clipboard));

    g_paste_clipboard_content_set_text (&priv->content, text);
}

static void
g_paste_clipboard_meta_select_text (GPasteClipboardMeta *self,
                                    const gchar         *text)
{
    GPasteClipboardMetaPrivate *priv = g_paste_clipboard_meta_get_instance_private (self);

    g_debug ("%s: select text", g_paste_clipboard_provider_target_name (priv->is_clipboard));

    g_paste_clipboard_meta_private_set_text (priv, text);

    g_autoptr (GBytes) bytes = g_bytes_new (text, strlen (text));
    GPasteClipboardMetaSource *source = g_object_new (G_PASTE_TYPE_CLIPBOARD_META_SOURCE, NULL);

    g_paste_clipboard_meta_source_add_text (source, bytes);
    g_paste_clipboard_meta_publish_source (self, source);
}

/* --- sync_text --- */

static void
g_paste_clipboard_meta_sync_ready (GPasteClipboardMeta *self G_GNUC_UNUSED,
                                   GBytes              *bytes,
                                   gpointer             user_data)
{
    GPasteClipboardMeta *other = user_data;

    if (!bytes)
        return;

    gsize size;
    const gchar *text = g_bytes_get_data (bytes, &size);

    if (text && g_utf8_validate (text, size, NULL))
    {
        g_autofree gchar *dup = g_strndup (text, size);
        g_paste_clipboard_meta_select_text (other, dup);
    }
}

static void
g_paste_clipboard_meta_sync_text (const GPasteClipboardMeta *self,
                                  GPasteClipboardMeta       *other)
{
    g_paste_clipboard_meta_read_mime ((GPasteClipboardMeta *) self, META_MIME_TEXT, g_paste_clipboard_meta_sync_ready, other);
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

static gboolean
g_paste_clipboard_meta_select_item (GPasteClipboardMeta *self,
                                    GPasteItem          *item)
{
    GPasteClipboardMetaPrivate *priv = g_paste_clipboard_meta_get_instance_private (self);

    g_debug ("%s: select item", g_paste_clipboard_provider_target_name (priv->is_clipboard));

    if (_G_PASTE_IS_IMAGE_ITEM (item))
    {
        GdkTexture *texture = g_paste_image_item_get_image (G_PASTE_IMAGE_ITEM (item));
        const gchar *checksum = g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (item));

        if (!texture)
            return FALSE;

        g_paste_clipboard_content_set_image_checksum (&priv->content, checksum);

        g_auto (GValue) value = G_VALUE_INIT;
        GPasteClipboardMetaSource *source = g_object_new (G_PASTE_TYPE_CLIPBOARD_META_SOURCE, NULL);

        g_value_init (&value, GDK_TYPE_TEXTURE);
        g_value_set_object (&value, texture);
        g_paste_clipboard_meta_source_add_value (source, &value);
        g_paste_clipboard_meta_publish_source (self, source);
        return TRUE;
    }

    if (_G_PASTE_IS_COLOR_ITEM (item))
    {
        const GdkRGBA *rgba = g_paste_color_item_get_rgba (G_PASTE_COLOR_ITEM (item));

        g_paste_clipboard_content_set_color (&priv->content, rgba);

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
        return TRUE;
    }

    if (_G_PASTE_IS_URIS_ITEM (item))
    {
        GdkFileList *file_list = g_paste_uris_item_get_file_list (G_PASTE_URIS_ITEM (item));

        g_paste_clipboard_content_set_file_list (&priv->content, file_list);

        g_auto (GValue) value = G_VALUE_INIT;
        GPasteClipboardMetaSource *source = g_object_new (G_PASTE_TYPE_CLIPBOARD_META_SOURCE, NULL);

        /* GDK advertises text/uri-list plus the portal filetransfer/files mimetypes
         * sandboxed apps need; x-special/gnome-copied-files rides as a special value. */
        g_value_init (&value, GDK_TYPE_FILE_LIST);
        g_value_set_boxed (&value, file_list);
        g_paste_clipboard_meta_source_add_value (source, &value);
        g_paste_clipboard_meta_source_add_special_values (source, item);
        g_paste_clipboard_meta_publish_source (self, source);
        return TRUE;
    }

    /* Plain text, with any rich-text/HTML/XML special values offered alongside. */
    const gchar *real_value = g_paste_item_get_real_value (item);
    g_paste_clipboard_meta_private_set_text (priv, real_value);

    g_autoptr (GBytes) bytes = g_bytes_new (real_value, strlen (real_value));
    GPasteClipboardMetaSource *source = g_object_new (G_PASTE_TYPE_CLIPBOARD_META_SOURCE, NULL);

    g_paste_clipboard_meta_source_add_text (source, bytes);
    g_paste_clipboard_meta_source_add_special_values (source, item);
    g_paste_clipboard_meta_publish_source (self, source);

    return TRUE;
}

static gboolean
g_paste_clipboard_meta_is_empty (const GPasteClipboardMeta *self)
{
    const GPasteClipboardMetaPrivate *priv = _g_paste_clipboard_meta_get_instance_private (self);

    return g_paste_clipboard_content_is_empty (&priv->content);
}

/* --- update --- */

typedef struct {
    GPasteClipboardMeta                  *self;
    GPasteClipboardProviderUpdateCallback callback;
    gpointer                              user_data;
    gint                                  pending;
    GPasteClipboardContentKind            content_kind;
    gboolean                              produced;
    gchar                                *text;
    GdkTexture                           *texture;
    gchar                                *mime;
    GdkFileList                          *file_list;
    GdkRGBA                               rgba;
    GPasteBinaryData                     *special_atom[G_PASTE_SPECIAL_ATOM_LAST];
} GPasteClipboardMetaUpdateData;

static void
g_paste_clipboard_meta_update_maybe_done (GPasteClipboardMetaUpdateData *data)
{
    if (--data->pending > 0)
        return;

    GPasteItem *item = NULL;

    if (data->produced)
    {
        switch (data->content_kind)
        {
        case CLIPBOARD_CONTENT_FILE_LIST:
            if (data->file_list)
                item = G_PASTE_ITEM (g_paste_uris_item_new (data->file_list));
            break;
        case CLIPBOARD_CONTENT_COLOR:
            item = G_PASTE_ITEM (g_paste_color_item_new (&data->rgba));
            break;
        case CLIPBOARD_CONTENT_TEXT:
            if (data->text)
                item = G_PASTE_ITEM (g_paste_text_item_new (data->text));
            break;
        case CLIPBOARD_CONTENT_IMAGE:
            if (data->texture)
                item = G_PASTE_ITEM (g_paste_image_item_new (data->texture));
            break;
        case CLIPBOARD_CONTENT_NONE:
            break;
        }
    }

    if (item &&
        (data->content_kind == CLIPBOARD_CONTENT_TEXT || data->content_kind == CLIPBOARD_CONTENT_FILE_LIST))
    {
        for (GPasteSpecialAtom atom = G_PASTE_SPECIAL_ATOM_FIRST; atom < G_PASTE_SPECIAL_ATOM_LAST; ++atom)
        {
            if (data->special_atom[atom])
                g_paste_item_add_special_value (item, g_steal_pointer (&data->special_atom[atom]));
        }
    }

    if (data->callback)
        data->callback (G_PASTE_CLIPBOARD_PROVIDER (data->self), item, data->user_data);

    for (GPasteSpecialAtom atom = G_PASTE_SPECIAL_ATOM_FIRST; atom < G_PASTE_SPECIAL_ATOM_LAST; ++atom)
        g_clear_object (&data->special_atom[atom]);
    g_clear_object (&data->texture);
    if (data->file_list)
        g_boxed_free (GDK_TYPE_FILE_LIST, g_steal_pointer (&data->file_list));
    g_free (data->text);
    g_free (data->mime);
    g_free (data);
}

static void
g_paste_clipboard_meta_update_on_text (GPasteClipboardMeta *self,
                                       GBytes              *bytes,
                                       gpointer             user_data)
{
    GPasteClipboardMetaUpdateData *data = user_data;
    GPasteClipboardMetaPrivate *priv = g_paste_clipboard_meta_get_instance_private (self);

    if (!bytes)
    {
        g_paste_clipboard_meta_update_maybe_done (data);
        return;
    }

    gsize size;
    const gchar *raw = g_bytes_get_data (bytes, &size);

    if (!raw || !g_utf8_validate (raw, size, NULL))
    {
        g_paste_clipboard_meta_update_maybe_done (data);
        return;
    }

    g_autofree gchar *text = g_strndup (raw, size);
    g_autofree gchar *value = NULL;

    switch (g_paste_clipboard_content_classify_text (&priv->content, priv->settings, priv->is_clipboard, text, &value))
    {
    case G_PASTE_CLIPBOARD_TEXT_REJECT:
        g_paste_clipboard_meta_update_maybe_done (data);
        return;
    case G_PASTE_CLIPBOARD_TEXT_RESELECT:
        g_paste_clipboard_meta_select_text (self, value);
        break;
    case G_PASTE_CLIPBOARD_TEXT_SET:
        g_paste_clipboard_meta_private_set_text (priv, value);
        break;
    }

    data->produced = TRUE;
    data->text = g_strdup (priv->content.str);
    g_paste_clipboard_meta_update_maybe_done (data);
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

static void
g_paste_clipboard_meta_update_on_value_deserialized (GObject      *source_object G_GNUC_UNUSED,
                                                     GAsyncResult *res,
                                                     gpointer      user_data)
{
    GPasteClipboardMetaUpdateData *data = user_data;
    GPasteClipboardMetaPrivate *priv = g_paste_clipboard_meta_get_instance_private (data->self);
    g_auto (GValue) value = G_VALUE_INIT;
    g_autoptr (GError) error = NULL;

    g_value_init (&value, g_paste_clipboard_meta_content_gtype (data->content_kind));

    if (!gdk_content_deserialize_finish (res, &value, &error))
    {
        if (error)
            g_debug ("Failed to decode selection: %s", error->message);
        g_paste_clipboard_meta_update_maybe_done (data);
        return;
    }

    switch (data->content_kind)
    {
    case CLIPBOARD_CONTENT_IMAGE:
    {
        g_autoptr (GdkTexture) texture = g_value_dup_object (&value);

        if (!texture)
            break;

        g_autofree gchar *checksum = g_paste_gtk_util_compute_checksum (texture);

        if (priv->content.kind == CLIPBOARD_CONTENT_IMAGE && g_paste_str_equal (checksum, priv->content.str))
            break;

        g_paste_clipboard_content_set_image_checksum (&priv->content, checksum);

        data->produced = TRUE;
        data->texture = g_steal_pointer (&texture);
        break;
    }
    case CLIPBOARD_CONTENT_COLOR:
    {
        const GdkRGBA *rgba = g_value_get_boxed (&value);

        if (!rgba || (priv->content.kind == CLIPBOARD_CONTENT_COLOR && gdk_rgba_equal (rgba, &priv->content.rgba)))
            break;

        g_paste_clipboard_content_set_color (&priv->content, rgba);

        data->produced = TRUE;
        data->rgba = *rgba;
        break;
    }
    case CLIPBOARD_CONTENT_FILE_LIST:
    {
        GdkFileList *file_list = g_value_get_boxed (&value);

        if (!file_list || !gdk_file_list_get_files (file_list))
            break;

        if (priv->content.kind == CLIPBOARD_CONTENT_FILE_LIST &&
            g_paste_clipboard_file_list_equal (priv->content.file_list, file_list))
            break;

        g_paste_clipboard_content_set_file_list (&priv->content, file_list);

        data->produced = TRUE;
        data->file_list = g_boxed_copy (GDK_TYPE_FILE_LIST, file_list);
        break;
    }
    default:
        break;
    }

    g_paste_clipboard_meta_update_maybe_done (data);
}

/* image/color/file-list all decode through GDK's deserialisers, the same path
 * the GDK backend's reads take, so every representation GDK accepts (and the
 * exact byte formats it expects) is handled identically here. */
static void
g_paste_clipboard_meta_update_on_value (GPasteClipboardMeta *self G_GNUC_UNUSED,
                                        GBytes              *bytes,
                                        gpointer             user_data)
{
    GPasteClipboardMetaUpdateData *data = user_data;

    if (!bytes)
    {
        g_paste_clipboard_meta_update_maybe_done (data);
        return;
    }

    g_autoptr (GInputStream) stream = g_memory_input_stream_new_from_bytes (bytes);

    gdk_content_deserialize_async (stream,
                                   data->mime,
                                   g_paste_clipboard_meta_content_gtype (data->content_kind),
                                   G_PRIORITY_DEFAULT,
                                   NULL, /* cancellable */
                                   g_paste_clipboard_meta_update_on_value_deserialized,
                                   data);
}

typedef struct {
    GPasteClipboardMetaUpdateData *data;
    GPasteSpecialAtom              atom;
} GPasteClipboardMetaAtomCtx;

static void
g_paste_clipboard_meta_on_atom_bytes (GPasteClipboardMeta *self G_GNUC_UNUSED,
                                      GBytes              *bytes,
                                      gpointer             user_data)
{
    g_autofree GPasteClipboardMetaAtomCtx *ctx = user_data;
    GPasteClipboardMetaUpdateData *data = ctx->data;

    if (bytes && g_bytes_get_size (bytes) > 0)
        data->special_atom[ctx->atom] = g_paste_binary_data_new (ctx->atom, g_bytes_ref (bytes));

    g_paste_clipboard_meta_update_maybe_done (data);
}

/* Pick the offered mimetype to read an image from: prefer PNG (what we publish
 * first and lossless), otherwise the first format GDK can deserialise into a
 * texture. Returns a pointer into @mimetypes, or NULL if none qualifies. */
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
                               GPasteClipboardProviderUpdateCallback callback,
                               gpointer                              user_data)
{
    GPasteClipboardMetaPrivate *priv = g_paste_clipboard_meta_get_instance_private (self);
    GList *mimetypes = meta_selection_get_mimetypes (priv->selection, priv->type);
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
    else if (g_paste_settings_get_images_support (priv->settings) &&
             (content_mime = g_paste_clipboard_meta_pick_mime (mimetypes, GDK_TYPE_TEXTURE, META_MIME_IMAGE)))
    {
        content_kind = CLIPBOARD_CONTENT_IMAGE;
    }
    else if (mimetypes_contain (mimetypes, META_MIME_TEXT) ||
             mimetypes_contain (mimetypes, META_MIME_TEXT_PLAIN))
    {
        content_kind = CLIPBOARD_CONTENT_TEXT;
        content_mime = mimetypes_contain (mimetypes, META_MIME_TEXT) ? META_MIME_TEXT : META_MIME_TEXT_PLAIN;
    }
    else
    {
        g_list_free_full (mimetypes, g_free);
        g_paste_clipboard_content_clear (&priv->content);
        if (callback)
            callback (G_PASTE_CLIPBOARD_PROVIDER (self), NULL, user_data);
        return;
    }

    GPasteClipboardMetaUpdateData *data = g_new0 (GPasteClipboardMetaUpdateData, 1);

    data->self = self;
    data->callback = callback;
    data->user_data = user_data;
    data->pending = 1;
    data->content_kind = content_kind;

    ++data->pending;
    switch (content_kind)
    {
    case CLIPBOARD_CONTENT_FILE_LIST:
    case CLIPBOARD_CONTENT_COLOR:
    case CLIPBOARD_CONTENT_IMAGE:
        /* Kept for the deferred deserialisation once the bytes have arrived. */
        data->mime = g_strdup (content_mime);
        g_paste_clipboard_meta_read_mime (self, content_mime, g_paste_clipboard_meta_update_on_value, data);
        break;
    case CLIPBOARD_CONTENT_TEXT:
        g_paste_clipboard_meta_read_mime (self, content_mime, g_paste_clipboard_meta_update_on_text, data);
        break;
    case CLIPBOARD_CONTENT_NONE:
        g_assert_not_reached ();
    }

    if (content_kind == CLIPBOARD_CONTENT_FILE_LIST ||
        (content_kind == CLIPBOARD_CONTENT_TEXT && g_paste_settings_get_rich_text_support (priv->settings)))
    {
        for (GPasteSpecialAtom atom = G_PASTE_SPECIAL_ATOM_FIRST; atom < G_PASTE_SPECIAL_ATOM_LAST; ++atom)
        {
            if (!mimetypes_contain (mimetypes, g_paste_special_atom_get (atom)))
                continue;

            GPasteClipboardMetaAtomCtx *ctx = g_new0 (GPasteClipboardMetaAtomCtx, 1);
            ctx->data = data;
            ctx->atom = atom;

            ++data->pending;
            g_paste_clipboard_meta_read_mime (self, g_paste_special_atom_get (atom), g_paste_clipboard_meta_on_atom_bytes, ctx);
        }
    }

    g_list_free_full (mimetypes, g_free);

    g_paste_clipboard_meta_update_maybe_done (data);
}

/* --- external ownership change --- */

static void
g_paste_clipboard_meta_on_owner_changed (GPasteClipboardMeta *self,
                                         guint                selection_type,
                                         MetaSelectionSource *source)
{
    const GPasteClipboardMetaPrivate *priv = _g_paste_clipboard_meta_get_instance_private (self);

    if ((MetaSelectionType) selection_type != priv->type)
        return;

    /* Our own writes come back here too: skip them, just like the GDK backend
     * skips gdk_clipboard_is_local() changes. */
    if (source && source == priv->owned_source)
        return;

    g_debug ("%s: owner change", g_paste_clipboard_provider_target_name (priv->is_clipboard));
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (self));
}

/* GPasteClipboardProvider interface adapters */
G_PASTE_CLIPBOARD_PROVIDER_DEFINE_VFUNCS (meta, META)

static void
g_paste_clipboard_meta_dispose (GObject *object)
{
    GPasteClipboardMetaPrivate *priv = g_paste_clipboard_meta_get_instance_private (G_PASTE_CLIPBOARD_META (object));

    if (priv->selection && priv->owner_changed_id)
    {
        g_clear_signal_handler (&priv->owner_changed_id, priv->selection);
        priv->owner_changed_id = 0;
    }
    g_clear_object (&priv->owned_source);
    g_clear_object (&priv->selection);
    g_clear_object (&priv->settings);

    G_OBJECT_CLASS (g_paste_clipboard_meta_parent_class)->dispose (object);
}

static void
g_paste_clipboard_meta_finalize (GObject *object)
{
    GPasteClipboardMetaPrivate *priv = g_paste_clipboard_meta_get_instance_private (G_PASTE_CLIPBOARD_META (object));

    g_paste_clipboard_content_clear (&priv->content);

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
    GPasteClipboardMeta *self = g_object_new (G_PASTE_TYPE_CLIPBOARD_META, NULL);
    GPasteClipboardMetaPrivate *priv = g_paste_clipboard_meta_get_instance_private (self);

    priv->selection = g_object_ref (selection);
    priv->type = is_clipboard ? META_SELECTION_CLIPBOARD : META_SELECTION_PRIMARY;
    priv->is_clipboard = is_clipboard;
    priv->settings = g_object_ref (settings);

    priv->owner_changed_id = g_signal_connect_swapped (selection,
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
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);

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
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);

    return _g_paste_clipboard_meta_new (selection, settings, FALSE);
}
