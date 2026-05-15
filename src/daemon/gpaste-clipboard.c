/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <string.h>

#include <gpaste-clipboard.h>
#include <gpaste-color-item.h>
#include <gpaste-image-item.h>
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

typedef enum
{
    CLIPBOARD_CONTENT_NONE,
    CLIPBOARD_CONTENT_TEXT,
    CLIPBOARD_CONTENT_IMAGE,
    CLIPBOARD_CONTENT_FILE_LIST,
    CLIPBOARD_CONTENT_COLOR,
} GPasteClipboardContent;

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

static void
g_paste_clipboard_on_text_ready (GObject      *source_object,
                                 GAsyncResult *res,
                                 gpointer      user_data)
{
    g_autofree GPasteClipboardTextCallbackData *data = user_data;
    GPasteClipboard *self = data->self;
    g_autoptr (GError) error = NULL;
    g_autofree gchar *text = gdk_clipboard_read_text_finish (GDK_CLIPBOARD (source_object), res, &error);

    if (!text)
    {
        if (error)
            g_debug ("Failed to read text from clipboard: %s", error->message);
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
g_paste_clipboard_set_text (GPasteClipboard            *self,
                            GPasteClipboardTextCallback callback,
                            gpointer                    user_data)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (self));

    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);
    GPasteClipboardTextCallbackData *data = g_new (GPasteClipboardTextCallbackData, 1);

    data->self = self;
    data->callback = callback;
    data->user_data = user_data;

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

    /* Avoid cycling twice as gdk_clipboard_set_text will make the clipboards manager react */
    g_paste_clipboard_private_set_text (priv, text);
    gdk_clipboard_set_text (priv->real, text);
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

    GSList *fa = gdk_file_list_get_files (a);
    GSList *fb = gdk_file_list_get_files (b);

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
    GPasteClipboard *self = data->self;
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

    if (priv->content_kind == CLIPBOARD_CONTENT_IMAGE && g_paste_str_equal (checksum, priv->str))
    {
        /* Same image, nothing to do */
    }
    else
    {
        g_paste_clipboard_private_select_texture (priv, texture, checksum);
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

    data->self = self;
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
    GPasteClipboard *self = data->self;
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

    data->self = self;
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
    g_autoptr (GError) error = NULL;
    g_autoptr (GBytes) bytes = g_input_stream_read_bytes_finish (G_INPUT_STREAM (source_object), res, &error);

    if (error || !bytes)
    {
        if (error)
            g_debug ("Failed to read special atom bytes: %s", error->message);
        if (data->callback)
            data->callback (data->self, data->atom, NULL, data->user_data);
        return;
    }

    if (data->callback)
        data->callback (data->self, data->atom, bytes, data->user_data);
}

static void
g_paste_clipboard_on_special_atom_stream_ready (GObject      *source_object,
                                                GAsyncResult *res,
                                                gpointer      user_data)
{
    g_autofree GPasteClipboardSpecialAtomData *data = user_data;
    g_autoptr (GError) error = NULL;
    const gchar *actual_mime = NULL;
    g_autoptr (GInputStream) stream = gdk_clipboard_read_finish (GDK_CLIPBOARD (source_object), res, &actual_mime, &error);

    if (error || !stream)
    {
        if (error)
            g_debug ("Failed to read special atom stream: %s", error->message);
        if (data->callback)
            data->callback (data->self, data->atom, NULL, data->user_data);
        return;
    }

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

    data->self = self;
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
    const gchar                  *text;
    const GdkRGBA                *rgba;
    GdkFileList                  *file_list;
    GdkTexture                   *texture;
    gboolean                      fallback;
    GPasteBinaryData             *special_atom[G_PASTE_SPECIAL_ATOM_LAST];
} GPasteClipboardUpdateData;

static void
g_paste_clipboard_update_maybe_done (GPasteClipboardUpdateData *data)
{
    if (--data->pending > 0)
        return;

    GPasteItem *item = NULL;

    if (data->file_list)
        item = G_PASTE_ITEM (g_paste_uris_item_new (data->file_list));
    else if (data->rgba)
        item = G_PASTE_ITEM (g_paste_color_item_new (data->rgba));
    else if (data->text)
        item = G_PASTE_ITEM (g_paste_text_item_new (data->text));
    else if (data->texture)
        item = G_PASTE_ITEM (g_paste_image_item_new (data->texture));

    if (item && !data->texture && !data->rgba)
    {
        for (GPasteSpecialAtom atom = G_PASTE_SPECIAL_ATOM_FIRST; atom < G_PASTE_SPECIAL_ATOM_LAST; ++atom)
        {
            if (data->special_atom[atom])
                g_paste_item_add_special_value (item, g_steal_pointer (&data->special_atom[atom]));
        }
    }

    if (data->callback)
        data->callback (data->self, item, data->fallback, data->user_data);

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

    if (!gdk_file_list_get_files (file_list))
    {
        g_paste_clipboard_update_maybe_done (data);
        return;
    }

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);

    if (g_paste_file_list_equal (priv->file_list, file_list))
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
 * g_paste_clipboard_update:
 * @self: a #GPasteClipboard instance
 * @callback: (scope async): the callback to be called when the clipboard content is ready
 * @user_data: user data to pass to @callback
 *
 * Read the current clipboard content and update the internal cache.
 * The callback receives a newly created #GPasteItem (or NULL if unchanged or empty)
 * and a fallback flag that is TRUE when the clipboard was empty and no content could be retrieved.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_update (GPasteClipboard              *self,
                          GPasteClipboardUpdateCallback callback,
                          gpointer                      user_data)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (self));

    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);
    GdkContentFormats *formats = gdk_clipboard_get_formats (priv->real);
    GPasteClipboardContent content_kind = CLIPBOARD_CONTENT_NONE;
    if (gdk_content_formats_contain_gtype (formats, GDK_TYPE_FILE_LIST))
        content_kind = CLIPBOARD_CONTENT_FILE_LIST;
    else if (gdk_content_formats_contain_gtype (formats, GDK_TYPE_RGBA))
        content_kind = CLIPBOARD_CONTENT_COLOR;
    else if (g_paste_settings_get_images_support (priv->settings) &&
             gdk_content_formats_contain_gtype (formats, GDK_TYPE_TEXTURE))
        content_kind = CLIPBOARD_CONTENT_IMAGE;
    else if (gdk_content_formats_contain_gtype (formats, G_TYPE_STRING))
        content_kind = CLIPBOARD_CONTENT_TEXT;

    GPasteClipboardUpdateData *data = g_new0 (GPasteClipboardUpdateData, 1);

    data->self = self;
    data->callback = callback;
    data->user_data = user_data;
    data->pending = 1;

    if (content_kind != CLIPBOARD_CONTENT_NONE)
    {
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
    }
    else
    {
        data->fallback = TRUE;
        ++data->pending;
        g_paste_clipboard_set_text (self, g_paste_clipboard_update_on_text_ready, data);
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
        g_ptr_array_add (providers, gdk_content_provider_new_typed (G_TYPE_STRING, real_value));
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
