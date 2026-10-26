// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <string.h>

#include <gpaste-clipboard.h>
#include <gpaste-color-item.h>
#include <gpaste-image-item.h>
#include <gpaste-text-content-provider.h>
#include <gpaste-uris-item.h>

struct _GPasteClipboard
{
    GObject parent_instance;
};

enum
{
    C_CHANGED,

    C_LAST_SIGNAL
};

typedef struct
{
    GdkClipboard          *real;
    gboolean               is_clipboard;
    GPasteSettings        *settings;

    GPasteClipboardContent content_kind;
    union {
        gchar       *str;
        GdkFileList *file_list;
        GdkRGBA      rgba;
    };

    guint64                c_signals[C_LAST_SIGNAL];
} GPasteClipboardPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (Clipboard, clipboard, G_TYPE_OBJECT)

enum
{
    CHANGED,

    LAST_SIGNAL
};

static guint64 signals[LAST_SIGNAL] = { 0 };

typedef void (*GPasteClipboardTextCallback)    (GPasteClipboard *self,
                                                const gchar     *text,
                                                gpointer         user_data);

typedef void (*GPasteClipboardTextureCallback) (GPasteClipboard *self,
                                                GdkTexture      *texture,
                                                gpointer         user_data);

/**
 * g_paste_clipboard_is_clipboard:
 * @self: a #GPasteClipboard instance
 *
 * Get whether this #GPasteClipboard is a clipboard or not (primary selection)
 *
 * Returns: %TRUE if this #GPasteClipboard is a clipboard
 */
G_PASTE_VISIBLE gboolean
g_paste_clipboard_is_clipboard (const GPasteClipboard *self)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIPBOARD (self), FALSE);

    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);

    return priv->is_clipboard;
}

/**
 * g_paste_clipboard_get_text:
 * @self: a #GPasteClipboard instance
 *
 * Get the text stored in the #GPasteClipboard
 *
 * Returns: read-only string containing the text or NULL
 */
G_PASTE_VISIBLE const gchar *
g_paste_clipboard_get_text (const GPasteClipboard *self)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIPBOARD (self), NULL);

    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);

    return (priv->content_kind == CLIPBOARD_CONTENT_TEXT) ? priv->str : NULL;
}

static const gchar *
_g_paste_clipboard_private_target_name (const GPasteClipboardPrivate *priv)
{
    return priv->is_clipboard ? "CLIPBOARD" : "PRIMARY";
}

static void
g_paste_clipboard_private_clear_content (GPasteClipboardPrivate *priv)
{
    switch (priv->content_kind)
    {
    case CLIPBOARD_CONTENT_TEXT:
    case CLIPBOARD_CONTENT_IMAGE:
        g_clear_pointer (&priv->str, g_free);
        break;
    case CLIPBOARD_CONTENT_FILE_LIST:
        g_boxed_free (GDK_TYPE_FILE_LIST, g_steal_pointer (&priv->file_list));
        break;
    case CLIPBOARD_CONTENT_COLOR:
    case CLIPBOARD_CONTENT_IGNORED:
    case CLIPBOARD_CONTENT_NONE:
        break;
    }
    priv->content_kind = CLIPBOARD_CONTENT_NONE;
}

static void
g_paste_clipboard_private_set_text (GPasteClipboardPrivate *priv,
                                    const gchar            *text)
{
    g_paste_clipboard_private_clear_content (priv);

    g_debug ("%s: set text", _g_paste_clipboard_private_target_name (priv));

    priv->content_kind = CLIPBOARD_CONTENT_TEXT;
    priv->str = g_strdup (text);
}

typedef struct {
    GPasteClipboard            *self;
    GPasteClipboardTextCallback callback;
    gpointer                    user_data;
} GPasteClipboardTextCallbackData;

/* What a set_text () read brought, @text %NULL for one that failed; frees
 * @user_data, its #GPasteClipboardTextCallbackData, and drops the ref it
 * holds. */
static void
g_paste_clipboard_text_read (gpointer     user_data,
                             const gchar *text)
{
    g_autofree GPasteClipboardTextCallbackData *data = user_data;
    g_autoptr (GPasteClipboard) self = data->self; /* ref taken in set_text */

    if (!text)
    {
        if (data->callback)
            data->callback (self, NULL, data->user_data);
        return;
    }

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);
    GPasteSettings *settings = priv->settings;
    g_autofree gchar *stripped = g_strstrip (g_strdup (text));
    gboolean trim_items = g_paste_settings_get_trim_items (settings);
    const gchar *to_add = trim_items ? stripped : text;
    guint64 length = strlen (to_add);

    if (length < g_paste_settings_get_min_text_item_size (settings) ||
        length > g_paste_settings_get_max_text_item_size (settings))
    {
        if (data->callback)
            data->callback (self, NULL, data->user_data);
        return;
    }
    if (priv->content_kind == CLIPBOARD_CONTENT_TEXT && g_paste_str_equal (priv->str, to_add))
    {
        if (data->callback)
            data->callback (self, NULL, data->user_data);
        return;
    }

    if (trim_items &&
        priv->is_clipboard &&
        !g_paste_str_equal (text, stripped))
            g_paste_clipboard_select_text (self, stripped);
    else
        g_paste_clipboard_private_set_text (priv, to_add);

    if (data->callback)
        data->callback (self, priv->str, data->user_data);
}

static void
g_paste_clipboard_on_text_ready (GObject      *source_object,
                                 GAsyncResult *res,
                                 gpointer      user_data)
{
    g_autoptr (GError) error = NULL;
    g_autofree gchar *text = gdk_clipboard_read_text_finish (GDK_CLIPBOARD (source_object), res, &error);

    if (error)
        g_debug ("Failed to read text from clipboard: %s", error->message);

    g_paste_clipboard_text_read (user_data, text);
}

/**
 * g_paste_clipboard_offers_bare_text:
 * @formats: what a selection offers
 *
 * Returns: whether the selection offers its text as a bare text/plain alone,
 *          which g_paste_clipboard_read_bare_text () says how to read
 */
gboolean
g_paste_clipboard_offers_bare_text (GdkContentFormats *formats)
{
    return !gdk_content_formats_contain_mime_type (formats, "text/plain;charset=utf-8") &&
           gdk_content_formats_contain_mime_type (formats, "text/plain");
}

/**
 * g_paste_clipboard_text_from_bytes:
 * @bytes: (nullable): what a text transfer brought, %NULL for one that failed
 * @not_utf8: (out): set when @bytes holds no UTF-8 text
 *
 * A text as it comes off a selection: up to its first NUL, which some owners
 * end it with, and the empty text for a transfer whose bytes hold no data at
 * all.
 *
 * Returns: (transfer full) (nullable): the text, %NULL for a failed transfer or
 *          one that is not UTF-8
 */
gchar *
g_paste_clipboard_text_from_bytes (GBytes   *bytes,
                                   gboolean *not_utf8)
{
    *not_utf8 = FALSE;
    if (!bytes)
        return NULL;

    gsize size = 0;
    const gchar *text = g_bytes_get_data (bytes, &size);

    if (!text)
        return g_strdup ("");

    const gchar *nul = memchr (text, '\0', size);

    if (nul)
        size = (gsize) (nul - text);
    if (!g_utf8_validate_len (text, size, NULL))
    {
        *not_utf8 = TRUE;
        return NULL;
    }

    return g_strndup (text, size);
}

/* What a bare text read brought, @text %NULL for one that failed. */
typedef void (*GPasteClipboardBareTextCallback) (gpointer     user_data,
                                                 const gchar *text);

typedef struct {
    GdkClipboard                   *real;
    GOutputStream                  *sink;
    GAsyncReadyCallback             fallback;
    GPasteClipboardBareTextCallback callback;
    gpointer                        user_data;
} GPasteClipboardBareTextData;

static void
g_paste_clipboard_bare_text_data_free (GPasteClipboardBareTextData *data)
{
    g_clear_object (&data->real);
    g_clear_object (&data->sink);
    g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GPasteClipboardBareTextData, g_paste_clipboard_bare_text_data_free)

/* Hands @data's callback the text @bytes holds, or has GDK's own read take
 * over, @data's fallback answering it, when they are not UTF-8 after all. */
static void
g_paste_clipboard_bare_text_done (GPasteClipboardBareTextData *data,
                                  GBytes                      *bytes)
{
    gboolean not_utf8 = FALSE;
    g_autofree gchar *text = g_paste_clipboard_text_from_bytes (bytes, &not_utf8);

    if (not_utf8)
        gdk_clipboard_read_text_async (data->real, NULL, data->fallback, data->user_data);
    else
        data->callback (data->user_data, text);
}

static void
g_paste_clipboard_on_bare_text_spliced (GObject      *source_object G_GNUC_UNUSED,
                                        GAsyncResult *res,
                                        gpointer      user_data)
{
    g_autoptr (GPasteClipboardBareTextData) data = user_data;
    g_autoptr (GError) error = NULL;
    g_autoptr (GBytes) bytes = NULL;

    if (g_output_stream_splice_finish (data->sink, res, &error) < 0)
        g_debug ("Failed to read text from clipboard: %s", error->message);
    else
        bytes = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (data->sink));

    g_paste_clipboard_bare_text_done (data, bytes);
}

static void
g_paste_clipboard_on_bare_text_stream (GObject      *source_object,
                                       GAsyncResult *res,
                                       gpointer      user_data)
{
    g_autoptr (GPasteClipboardBareTextData) data = user_data;
    g_autoptr (GError) error = NULL;
    g_autoptr (GInputStream) stream = gdk_clipboard_read_finish (GDK_CLIPBOARD (source_object), res, NULL, &error);

    if (!stream)
    {
        g_debug ("Failed to read text from clipboard: %s", error->message);
        g_paste_clipboard_bare_text_done (data, NULL);
        return;
    }

    /* Spliced whole: a single read of the stream brings only what has come so
     * far. */
    data->sink = g_memory_output_stream_new_resizable ();
    g_output_stream_splice_async (data->sink,
                                  stream,
                                  G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
                                  G_PRIORITY_DEFAULT,
                                  NULL, /* cancellable */
                                  g_paste_clipboard_on_bare_text_spliced,
                                  g_steal_pointer (&data));
}

/* What a selection offering nothing but a bare text/plain answers: GDK's text
 * read decodes that as ASCII, escaping every other byte as \XX, where the
 * convention X11 clients follow -- and g_paste_text_content_provider_new ()
 * serving one -- is UTF-8 under that name. Read as such, and through GDK's own
 * conversion only when it is not valid UTF-8 after all. Choosing this read is
 * g_paste_clipboard_offers_bare_text ()'s, and decoding what it brings
 * g_paste_clipboard_text_from_bytes ()'s, both tested; the read between them
 * takes a display and a selection owner of their own, which no test here has. */
static void
g_paste_clipboard_read_bare_text (GdkClipboard                   *real,
                                  GAsyncReadyCallback             fallback,
                                  GPasteClipboardBareTextCallback callback,
                                  gpointer                        user_data)
{
    GPasteClipboardBareTextData *data = g_new0 (GPasteClipboardBareTextData, 1);
    const gchar *mime_types[] = { "text/plain", NULL };

    data->real = g_object_ref (real);
    data->fallback = fallback;
    data->callback = callback;
    data->user_data = user_data;

    gdk_clipboard_read_async (real,
                              mime_types,
                              G_PRIORITY_DEFAULT,
                              NULL, /* cancellable */
                              g_paste_clipboard_on_bare_text_stream,
                              data);
}

static void
g_paste_clipboard_set_text (GPasteClipboard            *self,
                            GPasteClipboardTextCallback callback,
                            gpointer                    user_data)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (self));

    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);
    GPasteClipboardTextCallbackData *data = g_new (GPasteClipboardTextCallbackData, 1);

    /* Nothing else keeps us alive between the request and its callback: the
     * clipboard can be disposed while the selection owner takes its time (or
     * never answers at all). The callback drops this ref. */
    data->self = g_object_ref (self);
    data->callback = callback;
    data->user_data = user_data;

    if (g_paste_clipboard_offers_bare_text (gdk_clipboard_get_formats (priv->real)))
    {
        g_paste_clipboard_read_bare_text (priv->real, g_paste_clipboard_on_text_ready, g_paste_clipboard_text_read, data);
        return;
    }

    gdk_clipboard_read_text_async (priv->real,
                                   NULL, /* cancellable */
                                   g_paste_clipboard_on_text_ready,
                                   data);
}

/**
 * g_paste_clipboard_select_text:
 * @self: a #GPasteClipboard instance
 * @text: the text to select
 *
 * Put the text into the #GPasteClipboard and the intern GdkClipboard
 */
G_PASTE_VISIBLE void
g_paste_clipboard_select_text (GPasteClipboard *self,
                               const gchar     *text)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (self));
    g_return_if_fail (text);
    g_return_if_fail (g_utf8_validate (text, -1, NULL));

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);

    g_debug ("%s: select text", _g_paste_clipboard_private_target_name (priv));

    /* Avoid cycling twice as setting the content will make the clipboards manager react */
    g_paste_clipboard_private_set_text (priv, text);

    g_autoptr (GdkContentProvider) provider = g_paste_text_content_provider_new (text);

    gdk_clipboard_set_content (priv->real, provider);
}

static void
g_paste_clipboard_sync_ready (GObject      *source_object,
                              GAsyncResult *res,
                              gpointer      user_data)
{
    g_autoptr (GError) error = NULL;
    g_autofree gchar *text = gdk_clipboard_read_text_finish (GDK_CLIPBOARD (source_object), res, &error);

    if (error)
        g_debug ("Failed to sync clipboard text: %s", error->message);
    else if (text)
        g_paste_clipboard_select_text (user_data, text);
}

/* What a sync's bare text read brought (see
 * g_paste_clipboard_read_bare_text ()). */
static void
g_paste_clipboard_sync_text_read (gpointer     user_data,
                                  const gchar *text)
{
    if (text)
        g_paste_clipboard_select_text (user_data, text);
}

/**
 * g_paste_clipboard_sync_text:
 * @self: the source #GPasteClipboard instance
 * @other: the target #GPasteClipboard instance
 *
 * Synchronise the text between two clipboards
 */
G_PASTE_VISIBLE void
g_paste_clipboard_sync_text (const GPasteClipboard *self,
                             GPasteClipboard       *other)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (self));
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (other));

    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);

    if (g_paste_clipboard_offers_bare_text (gdk_clipboard_get_formats (priv->real)))
        g_paste_clipboard_read_bare_text (priv->real, g_paste_clipboard_sync_ready, g_paste_clipboard_sync_text_read, other);
    else
        gdk_clipboard_read_text_async (priv->real, NULL, g_paste_clipboard_sync_ready, other);
}

/**
 * g_paste_clipboard_clear:
 * @self: a #GPasteClipboard instance
 *
 * Clears the content of the clipboard
 */
G_PASTE_VISIBLE void
g_paste_clipboard_clear (GPasteClipboard *self)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (self));

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);

    /* We're already clear, don't enter an infinite event loop */
    if (priv->content_kind == CLIPBOARD_CONTENT_NONE)
        return;

    g_debug ("%s: clear", _g_paste_clipboard_private_target_name (priv));

    g_paste_clipboard_private_clear_content (priv);

    if (gdk_clipboard_is_local (priv->real))
        gdk_clipboard_set_content (priv->real, NULL);
}

static void
g_paste_clipboard_store_async_done (GObject      *source_object,
                                    GAsyncResult *res,
                                    gpointer      user_data G_GNUC_UNUSED)
{
    g_autoptr (GError) error = NULL;

    if (!gdk_clipboard_store_finish (GDK_CLIPBOARD (source_object), res, &error))
        g_warning ("Failed to store clipboard: %s", error->message);
}

/**
 * g_paste_clipboard_store:
 * @self: a #GPasteClipboard instance
 *
 * Store the contents of the clipboard before exiting
 */
G_PASTE_VISIBLE void
g_paste_clipboard_store (GPasteClipboard *self)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (self));

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);

    g_debug ("%s: store", _g_paste_clipboard_private_target_name (priv));

    gdk_clipboard_store_async (priv->real,
                               G_PRIORITY_DEFAULT,
                               NULL, /* cancellable */
                               g_paste_clipboard_store_async_done,
                               NULL);
}

/**
 * g_paste_clipboard_get_image_checksum:
 * @self: a #GPasteClipboard instance
 *
 * Get the checksum of the image stored in the #GPasteClipboard
 *
 * Returns: read-only string containing the checksum of the image stored in the #GPasteClipboard or NULL
 */
G_PASTE_VISIBLE const gchar *
g_paste_clipboard_get_image_checksum (const GPasteClipboard *self)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIPBOARD (self), NULL);

    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);

    return (priv->content_kind == CLIPBOARD_CONTENT_IMAGE) ? priv->str : NULL;
}

static void
g_paste_clipboard_private_set_image_checksum (GPasteClipboardPrivate *priv,
                                              const gchar            *image_checksum)
{
    g_paste_clipboard_private_clear_content (priv);
    priv->content_kind = CLIPBOARD_CONTENT_IMAGE;
    priv->str = g_strdup (image_checksum);
}

static gboolean
g_paste_file_list_equal (GdkFileList *a,
                         GdkFileList *b)
{
    if (a == b)
        return TRUE;
    if (!a || !b)
        return FALSE;

    /* gdk_file_list_get_files is (transfer container): the list is ours to
     * free, the #GFile it holds are not. */
    g_autoptr (GSList) files_a = gdk_file_list_get_files (a);
    g_autoptr (GSList) files_b = gdk_file_list_get_files (b);

    const GSList *fa = files_a;
    const GSList *fb = files_b;

    for (; fa && fb; fa = fa->next, fb = fb->next)
    {
        if (!g_file_equal (G_FILE (fa->data), G_FILE (fb->data)))
            return FALSE;
    }

    return !fa && !fb;
}

static void
g_paste_clipboard_private_set_color (GPasteClipboardPrivate *priv,
                                     const GdkRGBA          *rgba)
{
    g_paste_clipboard_private_clear_content (priv);

    g_debug ("%s: set color", _g_paste_clipboard_private_target_name (priv));

    priv->content_kind = CLIPBOARD_CONTENT_COLOR;
    priv->rgba = *rgba;
}

static void
g_paste_clipboard_private_set_file_list (GPasteClipboardPrivate *priv,
                                         GdkFileList            *file_list)
{
    g_paste_clipboard_private_clear_content (priv);

    g_debug ("%s: set file list", _g_paste_clipboard_private_target_name (priv));

    if (file_list)
    {
        priv->content_kind = CLIPBOARD_CONTENT_FILE_LIST;
        priv->file_list = g_boxed_copy (GDK_TYPE_FILE_LIST, file_list);
    }
}

static void
g_paste_clipboard_private_select_texture (GPasteClipboardPrivate *priv,
                                          GdkTexture             *texture,
                                          const gchar            *checksum)
{
    g_return_if_fail (GDK_IS_TEXTURE (texture));

    g_debug ("%s: select image", _g_paste_clipboard_private_target_name (priv));

    g_paste_clipboard_private_set_image_checksum (priv, checksum);
    gdk_clipboard_set (priv->real, GDK_TYPE_TEXTURE, texture);
}

typedef struct {
    GPasteClipboard                *self;
    GPasteClipboardTextureCallback  callback;
    gpointer                        user_data;
} GPasteClipboardTextureCallbackData;

static void
g_paste_clipboard_on_texture_ready (GObject      *source_object,
                                    GAsyncResult *res,
                                    gpointer      user_data)
{
    g_autofree GPasteClipboardTextureCallbackData *data = user_data;
    g_autoptr (GPasteClipboard) self = data->self; /* ref taken in set_texture */
    g_autoptr (GError) error = NULL;
    /* Transfer full — we own this ref */
    g_autoptr (GdkTexture) texture = gdk_clipboard_read_texture_finish (GDK_CLIPBOARD (source_object), res, &error);

    if (!texture)
    {
        if (error)
            g_debug ("Failed to read texture from clipboard: %s", error->message);
        if (data->callback)
            data->callback (self, NULL, data->user_data);
        return;
    }

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);
    g_autofree gchar *checksum = g_paste_gtk_util_compute_checksum (texture);
    GdkTexture *result = NULL;

    /* Not re-owned, as a text read is not unless trimmed: the selection stays
     * its owner's, with every format it offers. Once the owner goes, the
     * clipboard holds what a clipboard manager keeps -- mutter's, under GNOME
     * Shell -- and the history the image: a release reaches no update here,
     * the changed it makes being one with empty formats, which
     * on_real_changed () skips. No test shows it, since that takes a display
     * and a selection owner of their own. */
    if (priv->content_kind != CLIPBOARD_CONTENT_IMAGE || !g_paste_str_equal (checksum, priv->str))
    {
        g_paste_clipboard_private_set_image_checksum (priv, checksum);
        result = texture;  /* borrowed from the g_autoptr above */
    }

    if (data->callback)
        data->callback (self, result, data->user_data);
}

static void
g_paste_clipboard_set_texture (GPasteClipboard                *self,
                               GPasteClipboardTextureCallback  callback,
                               gpointer                        user_data)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (self));

    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);
    GPasteClipboardTextureCallbackData *data = g_new (GPasteClipboardTextureCallbackData, 1);

    /* Ref for the duration of the read (see set_text). */
    data->self = g_object_ref (self);
    data->callback = callback;
    data->user_data = user_data;

    gdk_clipboard_read_texture_async (priv->real,
                                      NULL, /* cancellable */
                                      g_paste_clipboard_on_texture_ready,
                                      data);
}

typedef void (*GPasteClipboardRGBACallback) (GPasteClipboard *self,
                                             const GdkRGBA   *rgba,
                                             gpointer         user_data);

typedef struct {
    GPasteClipboard             *self;
    GPasteClipboardRGBACallback  callback;
    gpointer                     user_data;
} GPasteClipboardRGBACallbackData;

static void
g_paste_clipboard_on_rgba_ready (GObject      *source_object,
                                 GAsyncResult *res,
                                 gpointer      user_data)
{
    g_autofree GPasteClipboardRGBACallbackData *data = user_data;
    g_autoptr (GPasteClipboard) self = data->self; /* ref taken in set_rgba */
    g_autoptr (GError) error = NULL;
    const GValue *value = gdk_clipboard_read_value_finish (GDK_CLIPBOARD (source_object), res, &error);

    if (!value)
    {
        if (error)
            g_debug ("Failed to read color from clipboard: %s", error->message);
        if (data->callback)
            data->callback (self, NULL, data->user_data);
        return;
    }

    const GdkRGBA *rgba = g_value_get_boxed (value);

    if (!rgba)
    {
        if (data->callback)
            data->callback (self, NULL, data->user_data);
        return;
    }

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);

    if (priv->content_kind == CLIPBOARD_CONTENT_COLOR && gdk_rgba_equal (rgba, &priv->rgba))
    {
        if (data->callback)
            data->callback (self, NULL, data->user_data);
        return;
    }

    g_paste_clipboard_private_set_color (priv, rgba);

    if (data->callback)
        data->callback (self, &priv->rgba, data->user_data);
}

static void
g_paste_clipboard_set_color (GPasteClipboard             *self,
                              GPasteClipboardRGBACallback  callback,
                              gpointer                     user_data)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (self));

    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);
    GPasteClipboardRGBACallbackData *data = g_new (GPasteClipboardRGBACallbackData, 1);

    /* Ref for the duration of the read (see set_text). */
    data->self = g_object_ref (self);
    data->callback = callback;
    data->user_data = user_data;

    gdk_clipboard_read_value_async (priv->real,
                                    GDK_TYPE_RGBA,
                                    G_PRIORITY_DEFAULT,
                                    NULL, /* cancellable */
                                    g_paste_clipboard_on_rgba_ready,
                                    data);
}

typedef void (*GPasteClipboardSpecialAtomCallback) (GPasteClipboard  *self,
                                                    GPasteSpecialAtom atom,
                                                    GBytes           *bytes,
                                                    gpointer          user_data);

typedef struct {
    GPasteClipboard                   *self;
    GPasteSpecialAtom                  atom;
    GPasteClipboardSpecialAtomCallback callback;
    gpointer                           user_data;
} GPasteClipboardSpecialAtomData;

static void
g_paste_clipboard_on_special_atom_bytes_ready (GObject      *source_object,
                                               GAsyncResult *res,
                                               gpointer      user_data)
{
    g_autofree GPasteClipboardSpecialAtomData *data = user_data;
    g_autoptr (GPasteClipboard) self = data->self; /* ref taken in fetch_special_atom */
    g_autoptr (GError) error = NULL;
    g_autoptr (GBytes) bytes = g_input_stream_read_bytes_finish (G_INPUT_STREAM (source_object), res, &error);

    if (error || !bytes)
    {
        if (error)
            g_debug ("Failed to read special atom bytes: %s", error->message);
        if (data->callback)
            data->callback (self, data->atom, NULL, data->user_data);
        return;
    }

    if (data->callback)
        data->callback (self, data->atom, bytes, data->user_data);
}

static void
g_paste_clipboard_on_special_atom_stream_ready (GObject      *source_object,
                                                GAsyncResult *res,
                                                gpointer      user_data)
{
    g_autofree GPasteClipboardSpecialAtomData *data = user_data;
    /* Released here unless the read below takes both it and @data over. */
    g_autoptr (GPasteClipboard) self = data->self; /* ref taken in fetch_special_atom */
    g_autoptr (GError) error = NULL;
    const gchar *actual_mime = NULL;
    g_autoptr (GInputStream) stream = gdk_clipboard_read_finish (GDK_CLIPBOARD (source_object), res, &actual_mime, &error);

    if (error || !stream)
    {
        if (error)
            g_debug ("Failed to read special atom stream: %s", error->message);
        if (data->callback)
            data->callback (self, data->atom, NULL, data->user_data);
        return;
    }

    /* data->self keeps the ref for the second half of the read. */
    g_steal_pointer (&self);

    g_input_stream_read_bytes_async (stream,
                                     G_MAXUINT,
                                     G_PRIORITY_DEFAULT,
                                     NULL, /* cancellable */
                                     g_paste_clipboard_on_special_atom_bytes_ready,
                                     g_steal_pointer (&data));
}

static void
g_paste_clipboard_fetch_special_atom (GPasteClipboard                   *self,
                                      GPasteSpecialAtom                  atom,
                                      GPasteClipboardSpecialAtomCallback callback,
                                      gpointer                           user_data)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (self));

    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);
    GPasteClipboardSpecialAtomData *data = g_new (GPasteClipboardSpecialAtomData, 1);

    /* Ref for the whole read (see set_text), across both of its halves. */
    data->self = g_object_ref (self);
    data->atom = atom;
    data->callback = callback;
    data->user_data = user_data;

    // TODO: should/can we fetch all mime types at once?
    const gchar *mime_types[] = { g_paste_special_atom_get (atom), NULL };

    gdk_clipboard_read_async (priv->real,
                              mime_types,
                              G_PRIORITY_DEFAULT,
                              NULL, /* cancellable */
                              g_paste_clipboard_on_special_atom_stream_ready,
                              data);
}

typedef struct {
    GPasteClipboard              *self;
    GPasteClipboardUpdateCallback callback;
    gpointer                      user_data;
    gint                          pending;
    GPasteClipboardContent        content_kind;
    union {
        const gchar   *text;
        GdkTexture    *texture;
        GdkFileList   *file_list;
        const GdkRGBA *rgba;
    };
    GPasteBinaryData             *special_atom[G_PASTE_SPECIAL_ATOM_LAST];
} GPasteClipboardUpdateData;

static void
g_paste_clipboard_update_maybe_done (GPasteClipboardUpdateData *data)
{
    if (--data->pending > 0)
        return;

    /* The ref taken in g_paste_clipboard_update, held for the whole barrier and
     * dropped here, once the callback it outlives has run. */
    g_autoptr (GPasteClipboard) self = data->self;
    GPasteItem *item = NULL;

    switch (data->content_kind)
    {
    case CLIPBOARD_CONTENT_FILE_LIST:
        if (data->file_list)
            item = G_PASTE_ITEM (g_paste_uris_item_new (data->file_list));
        break;
    case CLIPBOARD_CONTENT_COLOR:
        if (data->rgba)
            item = G_PASTE_ITEM (g_paste_color_item_new (data->rgba));
        break;
    case CLIPBOARD_CONTENT_TEXT:
        if (data->text)
            item = G_PASTE_ITEM (g_paste_text_item_new (data->text));
        break;
    case CLIPBOARD_CONTENT_IMAGE:
        if (data->texture)
            item = G_PASTE_ITEM (g_paste_image_item_new (data->texture));
        break;
    case CLIPBOARD_CONTENT_IGNORED:
    case CLIPBOARD_CONTENT_NONE:
        break;
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
        data->callback (self, item, data->user_data);

    for (GPasteSpecialAtom atom = G_PASTE_SPECIAL_ATOM_FIRST; atom < G_PASTE_SPECIAL_ATOM_LAST; ++atom)
        g_clear_object (&data->special_atom[atom]);
    g_free (data);
}

static void
g_paste_clipboard_update_on_file_list_ready (GObject      *source_object,
                                             GAsyncResult *res,
                                             gpointer      user_data)
{
    GPasteClipboardUpdateData *data = user_data;
    GPasteClipboard *self = data->self;
    g_autoptr (GError) error = NULL;
    const GValue *value = gdk_clipboard_read_value_finish (GDK_CLIPBOARD (source_object), res, &error);

    if (!value)
    {
        if (error)
            g_debug ("Failed to read file list from clipboard: %s", error->message);
        g_paste_clipboard_update_maybe_done (data);
        return;
    }

    GdkFileList *file_list = g_value_get_boxed (value);
    g_autoptr (GSList) files = (file_list) ? gdk_file_list_get_files (file_list) : NULL;

    if (!files)
    {
        g_paste_clipboard_update_maybe_done (data);
        return;
    }

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);

    /* file_list is only the live member of the union when that is the kind we
     * hold: reading it for anything else hands a string or a #GdkRGBA to
     * gdk_file_list_get_files (). */
    if (priv->content_kind == CLIPBOARD_CONTENT_FILE_LIST &&
        g_paste_file_list_equal (priv->file_list, file_list))
    {
        g_paste_clipboard_update_maybe_done (data);
        return;
    }

    g_paste_clipboard_private_set_file_list (priv, file_list);
    data->file_list = priv->file_list;

    g_paste_clipboard_update_maybe_done (data);
}

static void
g_paste_clipboard_fetch_file_list (GPasteClipboard           *self,
                                   GPasteClipboardUpdateData *data)
{
    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);

    gdk_clipboard_read_value_async (priv->real,
                                    GDK_TYPE_FILE_LIST,
                                    G_PRIORITY_DEFAULT,
                                    NULL, /* cancellable */
                                    g_paste_clipboard_update_on_file_list_ready,
                                    data);
}

static void
g_paste_clipboard_update_on_text_ready (GPasteClipboard *self G_GNUC_UNUSED,
                                        const gchar     *text,
                                        gpointer         user_data)
{
    GPasteClipboardUpdateData *data = user_data;
    data->text = text;
    g_paste_clipboard_update_maybe_done (data);
}

static void
g_paste_clipboard_update_on_texture_ready (GPasteClipboard *self G_GNUC_UNUSED,
                                           GdkTexture      *texture,
                                           gpointer         user_data)
{
    GPasteClipboardUpdateData *data = user_data;
    data->texture = texture;
    g_paste_clipboard_update_maybe_done (data);
}

static void
g_paste_clipboard_update_on_color_ready (GPasteClipboard *self G_GNUC_UNUSED,
                                         const GdkRGBA   *rgba,
                                         gpointer         user_data)
{
    GPasteClipboardUpdateData *data = user_data;
    data->rgba = rgba;
    g_paste_clipboard_update_maybe_done (data);
}

static void
g_paste_clipboard_update_on_special_atom_ready (GPasteClipboard  *self G_GNUC_UNUSED,
                                                GPasteSpecialAtom atom,
                                                GBytes           *bytes,
                                                gpointer          user_data)
{
    GPasteClipboardUpdateData *data = user_data;

    if (bytes && g_bytes_get_size (bytes) > 0)
        data->special_atom[atom] = g_paste_binary_data_new (atom, g_bytes_ref (bytes));

    g_paste_clipboard_update_maybe_done (data);
}

/**
 * g_paste_clipboard_content_for_formats:
 * @formats: what a selection offers
 * @images_support: whether images are read
 *
 * Returns: what an update reads out of a selection offering @formats:
 *          %CLIPBOARD_CONTENT_NONE for one with no owner, and
 *          %CLIPBOARD_CONTENT_IGNORED for one offering nothing GPaste reads
 */
GPasteClipboardContent
g_paste_clipboard_content_for_formats (GdkContentFormats *formats,
                                       gboolean           images_support)
{
    /* Text before an image: a copy offering both -- spreadsheet cells, which
     * come with a picture of themselves -- is the text, and an image is only
     * what is copied when it is all there is. An image offered beside a text
     * fallback, its address or a drawing's source, is recorded as that text
     * too: the formats alone do not tell the two apart, and text is what
     * pastes anywhere. */
    if (gdk_content_formats_contain_gtype (formats, GDK_TYPE_FILE_LIST))
        return CLIPBOARD_CONTENT_FILE_LIST;
    if (gdk_content_formats_contain_gtype (formats, GDK_TYPE_RGBA))
        return CLIPBOARD_CONTENT_COLOR;
    if (gdk_content_formats_contain_gtype (formats, G_TYPE_STRING))
        return CLIPBOARD_CONTENT_TEXT;
    if (images_support && gdk_content_formats_contain_gtype (formats, GDK_TYPE_TEXTURE))
        return CLIPBOARD_CONTENT_IMAGE;
    if (gdk_content_formats_is_empty (formats))
        return CLIPBOARD_CONTENT_NONE;
    return CLIPBOARD_CONTENT_IGNORED;
}

/**
 * g_paste_clipboard_update:
 * @self: a #GPasteClipboard instance
 * @callback: (scope async): the callback to be called when the clipboard content is ready
 * @user_data: user data to pass to @callback
 *
 * Read the current clipboard content and update the internal cache.
 * The callback receives a newly created #GPasteItem or NULL if the content is
 * unchanged, unrecognised, or the selection has no owner.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_update (GPasteClipboard              *self,
                          GPasteClipboardUpdateCallback callback,
                          gpointer                      user_data)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (self));

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);
    GdkContentFormats *formats = gdk_clipboard_get_formats (priv->real);
    GPasteClipboardContent content_kind = g_paste_clipboard_content_for_formats (formats, g_paste_settings_get_images_support (priv->settings));

    if (content_kind == CLIPBOARD_CONTENT_NONE)
    {
        /* No format offered: nobody owns the selection, or GDK has yet to
         * learn what its owner offers -- at startup, the clipboards manager
         * reading it before any change. Clear our cache: callers see an
         * empty clipboard and act accordingly (e.g. ensure_not_empty, which
         * selects the history's head). That leaves an owner GDK has not heard
         * from yet alone only because the daemon loads the history once the
         * clipboards are added, so that it is still empty here at startup. A
         * release later on never gets here (see on_texture_ready ()). */
        g_paste_clipboard_private_clear_content (priv);
        if (callback)
            callback (self, NULL, user_data);
        return;
    }
    else if (content_kind == CLIPBOARD_CONTENT_IGNORED)
    {
        /* The owner only provides types we don't handle (e.g. an image
         * while images-support is disabled). Don't track it, but flag the
         * clipboard as non-empty so ensure_not_empty doesn't override it. */
        g_paste_clipboard_private_clear_content (priv);
        priv->content_kind = CLIPBOARD_CONTENT_IGNORED;
        if (callback)
            callback (self, NULL, user_data);
        return;
    }

    GPasteClipboardUpdateData *data = g_new0 (GPasteClipboardUpdateData, 1);

    /* Held for the whole barrier -- the reads it fans out to and the callback
     * at the end -- and dropped in _update_maybe_done. */
    data->self = g_object_ref (self);
    data->callback = callback;
    data->user_data = user_data;
    data->pending = 1;
    data->content_kind = content_kind;

    gboolean atom_available[G_PASTE_SPECIAL_ATOM_LAST] = { FALSE };

    if (content_kind == CLIPBOARD_CONTENT_FILE_LIST ||
        (content_kind == CLIPBOARD_CONTENT_TEXT && g_paste_settings_get_rich_text_support (priv->settings)))
    {
        for (GPasteSpecialAtom atom = G_PASTE_SPECIAL_ATOM_FIRST; atom < G_PASTE_SPECIAL_ATOM_LAST; ++atom)
        {
            if (gdk_content_formats_contain_mime_type (formats, g_paste_special_atom_get (atom)))
                atom_available[atom] = TRUE;
        }
    }

    ++data->pending;
    switch (content_kind)
    {
    case CLIPBOARD_CONTENT_FILE_LIST:
        g_paste_clipboard_fetch_file_list (self, data);
        break;
    case CLIPBOARD_CONTENT_COLOR:
        g_paste_clipboard_set_color (self, g_paste_clipboard_update_on_color_ready, data);
        break;
    case CLIPBOARD_CONTENT_TEXT:
        g_paste_clipboard_set_text (self, g_paste_clipboard_update_on_text_ready, data);
        break;
    case CLIPBOARD_CONTENT_IMAGE:
        g_paste_clipboard_set_texture (self, g_paste_clipboard_update_on_texture_ready, data);
        break;
    case CLIPBOARD_CONTENT_IGNORED:
    case CLIPBOARD_CONTENT_NONE:
        g_assert_not_reached ();
    }

    for (GPasteSpecialAtom atom = G_PASTE_SPECIAL_ATOM_FIRST; atom < G_PASTE_SPECIAL_ATOM_LAST; ++atom)
    {
        if (atom_available[atom])
        {
            ++data->pending;
            g_paste_clipboard_fetch_special_atom (self, atom, g_paste_clipboard_update_on_special_atom_ready, data);
        }
    }

    g_paste_clipboard_update_maybe_done (data);
}

/**
 * g_paste_clipboard_select_item:
 * @self: a #GPasteClipboard instance
 * @item: the item to select
 *
 * Put the value of the item into the #GPasteClipboard and the intern GdkClipboard
 *
 * Returns: %FALSE if the item was invalid, %TRUE otherwise
 */
G_PASTE_VISIBLE gboolean
g_paste_clipboard_select_item (GPasteClipboard *self,
                               GPasteItem      *item)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIPBOARD (self), FALSE);
    g_return_val_if_fail (_G_PASTE_IS_ITEM (item), FALSE);

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);

    g_debug ("%s: select item", _g_paste_clipboard_private_target_name (priv));

    if (_G_PASTE_IS_IMAGE_ITEM (item))
    {
        GdkTexture *texture = g_paste_image_item_get_image (G_PASTE_IMAGE_ITEM (item));
        const gchar *checksum = g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (item));

        if (!texture)
            return FALSE;

        g_paste_clipboard_private_select_texture (priv, texture, checksum);
        return TRUE;
    }

    if (_G_PASTE_IS_COLOR_ITEM (item))
    {
        const GdkRGBA *rgba = g_paste_color_item_get_rgba (G_PASTE_COLOR_ITEM (item));
        g_paste_clipboard_private_set_color (priv, rgba);
        gdk_clipboard_set (priv->real, GDK_TYPE_RGBA, rgba);
        return TRUE;
    }

    g_autoptr (GPtrArray) providers = g_ptr_array_new ();

    if (_G_PASTE_IS_URIS_ITEM (item))
    {
        GdkFileList *file_list = g_paste_uris_item_get_file_list (G_PASTE_URIS_ITEM (item));
        g_paste_clipboard_private_set_file_list (priv, file_list);
        g_ptr_array_add (providers, gdk_content_provider_new_typed (GDK_TYPE_FILE_LIST, file_list));
    }
    else
    {
        const gchar *real_value = g_paste_item_get_real_value (item);
        g_paste_clipboard_private_set_text (priv, real_value);
        g_ptr_array_add (providers, g_paste_text_content_provider_new (real_value));
    }

    for (const GSList *sv = g_paste_item_get_special_values (item); sv; sv = sv->next)
    {
        const GPasteBinaryData *v = sv->data;
        g_ptr_array_add (providers, gdk_content_provider_new_for_bytes (g_paste_special_atom_get (g_paste_binary_data_get_mime (v)), g_paste_binary_data_get_bytes (v)));
    }

    g_autoptr (GdkContentProvider) provider = NULL;
    if (providers->len == 1)
        provider = g_ptr_array_index (providers, 0);
    else
        provider = gdk_content_provider_new_union ((GdkContentProvider **) providers->pdata, providers->len);

    gdk_clipboard_set_content (priv->real, provider);

    return TRUE;
}

/**
 * g_paste_clipboard_ensure_not_empty:
 * @self: a #GPasteClipboard instance
 * @history: a #GPasteHistory instance
 *
 * Ensure the clipboard has some contents (as long as the history's not empty)
 */
G_PASTE_VISIBLE void
g_paste_clipboard_ensure_not_empty (GPasteClipboard *self,
                                    GPasteHistory   *history)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (self));
    g_return_if_fail (_G_PASTE_IS_HISTORY (history));

    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);

    if (priv->content_kind != CLIPBOARD_CONTENT_NONE)
        return;

    const GList *hist = g_paste_history_get_history (history);

    if (hist)
    {
        GPasteItem *item = hist->data;

        if (!g_paste_clipboard_select_item (self, item))
            g_paste_history_remove (history, 0);
    }
}

static void
g_paste_clipboard_on_real_changed (GPasteClipboard *self)
{
    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);

    /* Unlike GTK3's owner-change, GdkClipboard::changed fires for local writes too.
     * Skip them to avoid re-processing our own clipboard content. */
    if (gdk_clipboard_is_local (priv->real))
        return;

    /* GTK4 fires changed twice per external selection event: once immediately
     * with empty formats (before TARGETS resolves) and once with the real
     * format list after TARGETS have been fetched. Only process the latter —
     * equivalent to GTK3 filtering out GDK_OWNER_CHANGE_DESTROY/CLOSE. */
    if (gdk_content_formats_is_empty (gdk_clipboard_get_formats (priv->real)))
        return;

    g_debug ("%s: owner change", _g_paste_clipboard_private_target_name (priv));
    g_signal_emit (self, signals[CHANGED], 0);
}

static void
g_paste_clipboard_dispose (GObject *object)
{
    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (G_PASTE_CLIPBOARD (object));

    if (priv->settings)
    {
        g_signal_handler_disconnect (priv->real, priv->c_signals[C_CHANGED]);
        g_clear_object (&priv->real);
        g_clear_object (&priv->settings);
    }

    G_OBJECT_CLASS (g_paste_clipboard_parent_class)->dispose (object);
}

static void
g_paste_clipboard_finalize (GObject *object)
{
    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (G_PASTE_CLIPBOARD (object));

    g_paste_clipboard_private_clear_content (priv);

    G_OBJECT_CLASS (g_paste_clipboard_parent_class)->finalize (object);
}

static void
g_paste_clipboard_class_init (GPasteClipboardClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_clipboard_dispose;
    object_class->finalize = g_paste_clipboard_finalize;

    /**
     * GPasteClipboard::changed:
     * @clipboard: the object on which the signal was emitted
     *
     * The "changed" signal is emitted when GPaste receives an event that
     * indicates that the ownership of the clipboard has changed.
     */
    signals[CHANGED] = g_signal_new ("changed",
                                     G_PASTE_TYPE_CLIPBOARD,
                                     G_SIGNAL_RUN_FIRST,
                                     0,    /* class offset     */
                                     NULL, /* accumulator      */
                                     NULL, /* accumulator data */
                                     g_cclosure_marshal_VOID__VOID,
                                     G_TYPE_NONE,
                                     0);
}

static void
g_paste_clipboard_init (GPasteClipboard *self G_GNUC_UNUSED)
{
}

static GPasteClipboard *
_g_paste_clipboard_new (GPasteSettings *settings,
                        gboolean        is_clipboard)
{
    GPasteClipboard *self = g_object_new (G_PASTE_TYPE_CLIPBOARD, NULL);
    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);

    GdkDisplay *display = gdk_display_get_default ();
    priv->real = g_object_ref (is_clipboard ? gdk_display_get_clipboard (display)
                                            : gdk_display_get_primary_clipboard (display));
    priv->is_clipboard = is_clipboard;
    priv->settings = g_object_ref (settings);

    priv->c_signals[C_CHANGED] = g_signal_connect_swapped (priv->real,
                                                           "changed",
                                                           G_CALLBACK (g_paste_clipboard_on_real_changed),
                                                           self);

    return self;
}

/**
 * g_paste_clipboard_new_clipboard:
 * @settings: a #GPasteSettings instance
 *
 * Create a new instance of #GPasteClipboard
 *
 * Returns: a newly allocated #GPasteClipboard
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClipboard *
g_paste_clipboard_new_clipboard (GPasteSettings *settings)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);

    return _g_paste_clipboard_new (settings, TRUE);
}

/**
 * g_paste_clipboard_new_primary:
 * @settings: a #GPasteSettings instance
 *
 * Create a new instance of #GPasteClipboard (primary selection)
 *
 * Returns: a newly allocated #GPasteClipboard
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClipboard *
g_paste_clipboard_new_primary (GPasteSettings *settings)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);

    return _g_paste_clipboard_new (settings, FALSE);
}
