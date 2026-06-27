// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-daemon/gpaste-clipboard-content.h>
#include <gpaste-daemon/gpaste-clipboard-gdk.h>
#include <gpaste-daemon/gpaste-color-item.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-special-atom.h>
#include <gpaste-daemon/gpaste-uris-item.h>

struct _GPasteClipboardGdk
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

    GPasteClipboardContent content;

    guint64                c_signals[C_LAST_SIGNAL];
} GPasteClipboardGdkPrivate;

static void g_paste_clipboard_gdk_provider_iface_init (GPasteClipboardProviderInterface *iface);

G_PASTE_DEFINE_TYPE_WITH_PRIVATE_AND_INTERFACE (ClipboardGdk, clipboard_gdk, G_TYPE_OBJECT,
                                                G_PASTE_TYPE_CLIPBOARD_PROVIDER, g_paste_clipboard_gdk_provider_iface_init)

typedef void (*GPasteClipboardGdkTextCallback)    (GPasteClipboardGdk *self,
                                                   const gchar        *text,
                                                   gpointer            user_data);

typedef void (*GPasteClipboardGdkTextureCallback) (GPasteClipboardGdk *self,
                                                   GdkTexture         *texture,
                                                   gpointer            user_data);

static gboolean
g_paste_clipboard_gdk_is_clipboard (const GPasteClipboardGdk *self)
{
    const GPasteClipboardGdkPrivate *priv = _g_paste_clipboard_gdk_get_instance_private (self);

    return priv->is_clipboard;
}

static const gchar *
g_paste_clipboard_gdk_get_text (const GPasteClipboardGdk *self)
{
    const GPasteClipboardGdkPrivate *priv = _g_paste_clipboard_gdk_get_instance_private (self);

    return g_paste_clipboard_content_get_text (&priv->content);
}

static void
g_paste_clipboard_gdk_private_set_text (GPasteClipboardGdkPrivate *priv,
                                        const gchar               *text)
{
    g_debug ("%s: set text", g_paste_clipboard_provider_target_name (priv->is_clipboard));

    g_paste_clipboard_content_set_text (&priv->content, text);
}

static void g_paste_clipboard_gdk_select_text (GPasteClipboardGdk *self,
                                               const gchar        *text);

typedef struct {
    GPasteClipboardGdk            *self;
    GPasteClipboardGdkTextCallback callback;
    gpointer                       user_data;
} GPasteClipboardGdkTextCallbackData;

static void
g_paste_clipboard_gdk_on_text_ready (GObject      *source_object,
                                     GAsyncResult *res,
                                     gpointer      user_data)
{
    g_autofree GPasteClipboardGdkTextCallbackData *data = user_data;
    GPasteClipboardGdk *self = data->self;
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

    GPasteClipboardGdkPrivate *priv = g_paste_clipboard_gdk_get_instance_private (self);
    g_autofree gchar *value = NULL;

    switch (g_paste_clipboard_content_classify_text (&priv->content, priv->settings, priv->is_clipboard, text, &value))
    {
    case G_PASTE_CLIPBOARD_TEXT_REJECT:
        if (data->callback)
            data->callback (self, NULL, data->user_data);
        return;
    case G_PASTE_CLIPBOARD_TEXT_RESELECT:
        g_paste_clipboard_gdk_select_text (self, value);
        break;
    case G_PASTE_CLIPBOARD_TEXT_SET:
        g_paste_clipboard_gdk_private_set_text (priv, value);
        break;
    }

    if (data->callback)
        data->callback (self, priv->content.str, data->user_data);
}

static void
g_paste_clipboard_gdk_set_text (GPasteClipboardGdk            *self,
                                GPasteClipboardGdkTextCallback callback,
                                gpointer                       user_data)
{
    const GPasteClipboardGdkPrivate *priv = _g_paste_clipboard_gdk_get_instance_private (self);
    GPasteClipboardGdkTextCallbackData *data = g_new (GPasteClipboardGdkTextCallbackData, 1);

    data->self = self;
    data->callback = callback;
    data->user_data = user_data;

    gdk_clipboard_read_text_async (priv->real,
                                   NULL, /* cancellable */
                                   g_paste_clipboard_gdk_on_text_ready,
                                   data);
}

static void
g_paste_clipboard_gdk_select_text (GPasteClipboardGdk *self,
                                   const gchar        *text)
{
    GPasteClipboardGdkPrivate *priv = g_paste_clipboard_gdk_get_instance_private (self);

    g_debug ("%s: select text", g_paste_clipboard_provider_target_name (priv->is_clipboard));

    /* Avoid cycling twice as gdk_clipboard_set_text will make the clipboards manager react */
    g_paste_clipboard_gdk_private_set_text (priv, text);
    gdk_clipboard_set_text (priv->real, text);
}

static void
g_paste_clipboard_gdk_sync_ready (GObject      *source_object,
                                  GAsyncResult *res,
                                  gpointer      user_data)
{
    g_autoptr (GError) error = NULL;
    g_autofree gchar *text = gdk_clipboard_read_text_finish (GDK_CLIPBOARD (source_object), res, &error);

    if (error)
        g_debug ("Failed to sync clipboard text: %s", error->message);
    else if (text)
        g_paste_clipboard_gdk_select_text (user_data, text);
}

static void
g_paste_clipboard_gdk_sync_text (const GPasteClipboardGdk *self,
                                 GPasteClipboardGdk       *other)
{
    const GPasteClipboardGdkPrivate *priv = _g_paste_clipboard_gdk_get_instance_private (self);

    gdk_clipboard_read_text_async (priv->real, NULL, g_paste_clipboard_gdk_sync_ready, other);
}

static void
g_paste_clipboard_gdk_store_async_done (GObject      *source_object,
                                        GAsyncResult *res,
                                        gpointer      user_data G_GNUC_UNUSED)
{
    g_autoptr (GError) error = NULL;

    if (!gdk_clipboard_store_finish (GDK_CLIPBOARD (source_object), res, &error))
        g_warning ("Failed to store clipboard: %s", error->message);
}

static void
g_paste_clipboard_gdk_store (GPasteClipboardGdk *self)
{
    GPasteClipboardGdkPrivate *priv = g_paste_clipboard_gdk_get_instance_private (self);

    g_debug ("%s: store", g_paste_clipboard_provider_target_name (priv->is_clipboard));

    gdk_clipboard_store_async (priv->real,
                               G_PRIORITY_DEFAULT,
                               NULL, /* cancellable */
                               g_paste_clipboard_gdk_store_async_done,
                               NULL);
}

static const gchar *
g_paste_clipboard_gdk_get_image_checksum (const GPasteClipboardGdk *self)
{
    const GPasteClipboardGdkPrivate *priv = _g_paste_clipboard_gdk_get_instance_private (self);

    return g_paste_clipboard_content_get_image_checksum (&priv->content);
}

static void
g_paste_clipboard_gdk_private_set_image_checksum (GPasteClipboardGdkPrivate *priv,
                                                  const gchar               *image_checksum)
{
    g_paste_clipboard_content_set_image_checksum (&priv->content, image_checksum);
}

static void
g_paste_clipboard_gdk_private_set_color (GPasteClipboardGdkPrivate *priv,
                                         const GdkRGBA             *rgba)
{
    g_debug ("%s: set color", g_paste_clipboard_provider_target_name (priv->is_clipboard));

    g_paste_clipboard_content_set_color (&priv->content, rgba);
}

static void
g_paste_clipboard_gdk_private_set_file_list (GPasteClipboardGdkPrivate *priv,
                                             GdkFileList               *file_list)
{
    g_debug ("%s: set file list", g_paste_clipboard_provider_target_name (priv->is_clipboard));

    g_paste_clipboard_content_set_file_list (&priv->content, file_list);
}

static void
g_paste_clipboard_gdk_private_select_texture (GPasteClipboardGdkPrivate *priv,
                                              GdkTexture                *texture,
                                              const gchar               *checksum)
{
    g_return_if_fail (GDK_IS_TEXTURE (texture));

    g_debug ("%s: select image", g_paste_clipboard_provider_target_name (priv->is_clipboard));

    g_paste_clipboard_gdk_private_set_image_checksum (priv, checksum);
    gdk_clipboard_set (priv->real, GDK_TYPE_TEXTURE, texture);
}

typedef struct {
    GPasteClipboardGdk               *self;
    GPasteClipboardGdkTextureCallback callback;
    gpointer                          user_data;
} GPasteClipboardGdkTextureCallbackData;

static void
g_paste_clipboard_gdk_on_texture_ready (GObject      *source_object,
                                        GAsyncResult *res,
                                        gpointer      user_data)
{
    g_autofree GPasteClipboardGdkTextureCallbackData *data = user_data;
    GPasteClipboardGdk *self = data->self;
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

    GPasteClipboardGdkPrivate *priv = g_paste_clipboard_gdk_get_instance_private (self);
    g_autofree gchar *checksum = g_paste_gtk_util_compute_checksum (texture);
    GdkTexture *result = NULL;

    if (priv->content.kind == CLIPBOARD_CONTENT_IMAGE && g_paste_str_equal (checksum, priv->content.str))
    {
        /* Same image, nothing to do */
    }
    else
    {
        g_paste_clipboard_gdk_private_select_texture (priv, texture, checksum);
        result = texture;  /* borrowed from the g_autoptr above */
    }

    if (data->callback)
        data->callback (self, result, data->user_data);
}

static void
g_paste_clipboard_gdk_set_texture (GPasteClipboardGdk               *self,
                                   GPasteClipboardGdkTextureCallback callback,
                                   gpointer                          user_data)
{
    const GPasteClipboardGdkPrivate *priv = _g_paste_clipboard_gdk_get_instance_private (self);
    GPasteClipboardGdkTextureCallbackData *data = g_new (GPasteClipboardGdkTextureCallbackData, 1);

    data->self = self;
    data->callback = callback;
    data->user_data = user_data;

    gdk_clipboard_read_texture_async (priv->real,
                                      NULL, /* cancellable */
                                      g_paste_clipboard_gdk_on_texture_ready,
                                      data);
}

typedef void (*GPasteClipboardGdkRGBACallback) (GPasteClipboardGdk *self,
                                               const GdkRGBA       *rgba,
                                               gpointer             user_data);

typedef struct {
    GPasteClipboardGdk            *self;
    GPasteClipboardGdkRGBACallback callback;
    gpointer                       user_data;
} GPasteClipboardGdkRGBACallbackData;

static void
g_paste_clipboard_gdk_on_rgba_ready (GObject      *source_object,
                                     GAsyncResult *res,
                                     gpointer      user_data)
{
    g_autofree GPasteClipboardGdkRGBACallbackData *data = user_data;
    GPasteClipboardGdk *self = data->self;
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

    GPasteClipboardGdkPrivate *priv = g_paste_clipboard_gdk_get_instance_private (self);

    if (priv->content.kind == CLIPBOARD_CONTENT_COLOR && gdk_rgba_equal (rgba, &priv->content.rgba))
    {
        if (data->callback)
            data->callback (self, NULL, data->user_data);
        return;
    }

    g_paste_clipboard_gdk_private_set_color (priv, rgba);

    if (data->callback)
        data->callback (self, &priv->content.rgba, data->user_data);
}

static void
g_paste_clipboard_gdk_set_color (GPasteClipboardGdk            *self,
                                 GPasteClipboardGdkRGBACallback callback,
                                 gpointer                       user_data)
{
    const GPasteClipboardGdkPrivate *priv = _g_paste_clipboard_gdk_get_instance_private (self);
    GPasteClipboardGdkRGBACallbackData *data = g_new (GPasteClipboardGdkRGBACallbackData, 1);

    data->self = self;
    data->callback = callback;
    data->user_data = user_data;

    gdk_clipboard_read_value_async (priv->real,
                                    GDK_TYPE_RGBA,
                                    G_PRIORITY_DEFAULT,
                                    NULL, /* cancellable */
                                    g_paste_clipboard_gdk_on_rgba_ready,
                                    data);
}

typedef void (*GPasteClipboardGdkSpecialAtomCallback) (GPasteClipboardGdk *self,
                                                       GPasteSpecialAtom   atom,
                                                       GBytes             *bytes,
                                                       gpointer            user_data);

typedef struct {
    GPasteClipboardGdk                  *self;
    GPasteSpecialAtom                    atom;
    GPasteClipboardGdkSpecialAtomCallback callback;
    gpointer                             user_data;
} GPasteClipboardGdkSpecialAtomData;

static void
g_paste_clipboard_gdk_on_special_atom_bytes_ready (GObject      *source_object,
                                                   GAsyncResult *res,
                                                   gpointer      user_data)
{
    g_autofree GPasteClipboardGdkSpecialAtomData *data = user_data;
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
g_paste_clipboard_gdk_on_special_atom_stream_ready (GObject      *source_object,
                                                    GAsyncResult *res,
                                                    gpointer      user_data)
{
    g_autofree GPasteClipboardGdkSpecialAtomData *data = user_data;
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
                                     g_paste_clipboard_gdk_on_special_atom_bytes_ready,
                                     g_steal_pointer (&data));
}

static void
g_paste_clipboard_gdk_fetch_special_atom (GPasteClipboardGdk                   *self,
                                          GPasteSpecialAtom                     atom,
                                          GPasteClipboardGdkSpecialAtomCallback callback,
                                          gpointer                              user_data)
{
    const GPasteClipboardGdkPrivate *priv = _g_paste_clipboard_gdk_get_instance_private (self);
    GPasteClipboardGdkSpecialAtomData *data = g_new (GPasteClipboardGdkSpecialAtomData, 1);

    data->self = self;
    data->atom = atom;
    data->callback = callback;
    data->user_data = user_data;

    /* gdk_clipboard_read_async() resolves to a single stream (the first of the
     * requested mimetypes the owner provides), so distinct atoms cannot be
     * collapsed into one read; update() already fires these reads in parallel. */
    const gchar *mime_types[] = { g_paste_special_atom_get (atom), NULL };

    gdk_clipboard_read_async (priv->real,
                              mime_types,
                              G_PRIORITY_DEFAULT,
                              NULL, /* cancellable */
                              g_paste_clipboard_gdk_on_special_atom_stream_ready,
                              data);
}

typedef struct {
    GPasteClipboardGdk                   *self;
    GPasteClipboardProviderUpdateCallback callback;
    gpointer                              user_data;
    gint                                  pending;
    GPasteClipboardContentKind            content_kind;
    union {
        const gchar   *text;
        GdkTexture    *texture;
        GdkFileList   *file_list;
        const GdkRGBA *rgba;
    };
    GPasteBinaryData                     *special_atom[G_PASTE_SPECIAL_ATOM_LAST];
} GPasteClipboardGdkUpdateData;

static void
g_paste_clipboard_gdk_update_maybe_done (GPasteClipboardGdkUpdateData *data)
{
    if (--data->pending > 0)
        return;

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
        data->callback (G_PASTE_CLIPBOARD_PROVIDER (data->self), item, data->user_data);

    for (GPasteSpecialAtom atom = G_PASTE_SPECIAL_ATOM_FIRST; atom < G_PASTE_SPECIAL_ATOM_LAST; ++atom)
        g_clear_object (&data->special_atom[atom]);
    g_free (data);
}

static void
g_paste_clipboard_gdk_update_on_file_list_ready (GObject      *source_object,
                                                 GAsyncResult *res,
                                                 gpointer      user_data)
{
    GPasteClipboardGdkUpdateData *data = user_data;
    GPasteClipboardGdk *self = data->self;
    g_autoptr (GError) error = NULL;
    const GValue *value = gdk_clipboard_read_value_finish (GDK_CLIPBOARD (source_object), res, &error);

    if (!value)
    {
        if (error)
            g_debug ("Failed to read file list from clipboard: %s", error->message);
        g_paste_clipboard_gdk_update_maybe_done (data);
        return;
    }

    GdkFileList *file_list = g_value_get_boxed (value);

    if (!gdk_file_list_get_files (file_list))
    {
        g_paste_clipboard_gdk_update_maybe_done (data);
        return;
    }

    GPasteClipboardGdkPrivate *priv = g_paste_clipboard_gdk_get_instance_private (self);

    if (g_paste_clipboard_file_list_equal (priv->content.file_list, file_list))
    {
        g_paste_clipboard_gdk_update_maybe_done (data);
        return;
    }

    g_paste_clipboard_gdk_private_set_file_list (priv, file_list);
    data->file_list = priv->content.file_list;

    g_paste_clipboard_gdk_update_maybe_done (data);
}

static void
g_paste_clipboard_gdk_fetch_file_list (GPasteClipboardGdk           *self,
                                       GPasteClipboardGdkUpdateData *data)
{
    const GPasteClipboardGdkPrivate *priv = _g_paste_clipboard_gdk_get_instance_private (self);

    gdk_clipboard_read_value_async (priv->real,
                                    GDK_TYPE_FILE_LIST,
                                    G_PRIORITY_DEFAULT,
                                    NULL, /* cancellable */
                                    g_paste_clipboard_gdk_update_on_file_list_ready,
                                    data);
}

static void
g_paste_clipboard_gdk_update_on_text_ready (GPasteClipboardGdk *self G_GNUC_UNUSED,
                                            const gchar        *text,
                                            gpointer            user_data)
{
    GPasteClipboardGdkUpdateData *data = user_data;
    data->text = text;
    g_paste_clipboard_gdk_update_maybe_done (data);
}

static void
g_paste_clipboard_gdk_update_on_texture_ready (GPasteClipboardGdk *self G_GNUC_UNUSED,
                                               GdkTexture         *texture,
                                               gpointer            user_data)
{
    GPasteClipboardGdkUpdateData *data = user_data;
    data->texture = texture;
    g_paste_clipboard_gdk_update_maybe_done (data);
}

static void
g_paste_clipboard_gdk_update_on_color_ready (GPasteClipboardGdk *self G_GNUC_UNUSED,
                                             const GdkRGBA      *rgba,
                                             gpointer            user_data)
{
    GPasteClipboardGdkUpdateData *data = user_data;
    data->rgba = rgba;
    g_paste_clipboard_gdk_update_maybe_done (data);
}

static void
g_paste_clipboard_gdk_update_on_special_atom_ready (GPasteClipboardGdk *self G_GNUC_UNUSED,
                                                    GPasteSpecialAtom   atom,
                                                    GBytes             *bytes,
                                                    gpointer            user_data)
{
    GPasteClipboardGdkUpdateData *data = user_data;

    if (bytes && g_bytes_get_size (bytes) > 0)
        data->special_atom[atom] = g_paste_binary_data_new (atom, g_bytes_ref (bytes));

    g_paste_clipboard_gdk_update_maybe_done (data);
}

static void
g_paste_clipboard_gdk_update (GPasteClipboardGdk                   *self,
                              GPasteClipboardProviderUpdateCallback callback,
                              gpointer                              user_data)
{
    GPasteClipboardGdkPrivate *priv = g_paste_clipboard_gdk_get_instance_private (self);
    GdkContentFormats *formats = gdk_clipboard_get_formats (priv->real);
    GPasteClipboardContentKind content_kind = CLIPBOARD_CONTENT_NONE;
    if (gdk_content_formats_contain_gtype (formats, GDK_TYPE_FILE_LIST))
        content_kind = CLIPBOARD_CONTENT_FILE_LIST;
    else if (gdk_content_formats_contain_gtype (formats, GDK_TYPE_RGBA))
        content_kind = CLIPBOARD_CONTENT_COLOR;
    else if (g_paste_settings_get_images_support (priv->settings) &&
             gdk_content_formats_contain_gtype (formats, GDK_TYPE_TEXTURE))
        content_kind = CLIPBOARD_CONTENT_IMAGE;
    else if (gdk_content_formats_contain_gtype (formats, G_TYPE_STRING))
        content_kind = CLIPBOARD_CONTENT_TEXT;
    else
    {
        /* No recognized content: the selection was released or the owner
         * provides no type we handle. Clear our cache so callers see an
         * empty clipboard and act accordingly (e.g. ensure_not_empty). */
        g_paste_clipboard_content_clear (&priv->content);
        if (callback)
            callback (G_PASTE_CLIPBOARD_PROVIDER (self), NULL, user_data);
        return;
    }

    GPasteClipboardGdkUpdateData *data = g_new0 (GPasteClipboardGdkUpdateData, 1);

    data->self = self;
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
        g_paste_clipboard_gdk_fetch_file_list (self, data);
        break;
    case CLIPBOARD_CONTENT_COLOR:
        g_paste_clipboard_gdk_set_color (self, g_paste_clipboard_gdk_update_on_color_ready, data);
        break;
    case CLIPBOARD_CONTENT_TEXT:
        g_paste_clipboard_gdk_set_text (self, g_paste_clipboard_gdk_update_on_text_ready, data);
        break;
    case CLIPBOARD_CONTENT_IMAGE:
        g_paste_clipboard_gdk_set_texture (self, g_paste_clipboard_gdk_update_on_texture_ready, data);
        break;
    case CLIPBOARD_CONTENT_NONE:
        g_assert_not_reached ();
    }

    for (GPasteSpecialAtom atom = G_PASTE_SPECIAL_ATOM_FIRST; atom < G_PASTE_SPECIAL_ATOM_LAST; ++atom)
    {
        if (atom_available[atom])
        {
            ++data->pending;
            g_paste_clipboard_gdk_fetch_special_atom (self, atom, g_paste_clipboard_gdk_update_on_special_atom_ready, data);
        }
    }

    g_paste_clipboard_gdk_update_maybe_done (data);
}

static gboolean
g_paste_clipboard_gdk_select_item (GPasteClipboardGdk *self,
                                   GPasteItem         *item)
{
    GPasteClipboardGdkPrivate *priv = g_paste_clipboard_gdk_get_instance_private (self);

    g_debug ("%s: select item", g_paste_clipboard_provider_target_name (priv->is_clipboard));

    if (_G_PASTE_IS_IMAGE_ITEM (item))
    {
        GdkTexture *texture = g_paste_image_item_get_image (G_PASTE_IMAGE_ITEM (item));
        const gchar *checksum = g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (item));

        if (!texture)
            return FALSE;

        g_paste_clipboard_gdk_private_select_texture (priv, texture, checksum);
        return TRUE;
    }

    if (_G_PASTE_IS_COLOR_ITEM (item))
    {
        const GdkRGBA *rgba = g_paste_color_item_get_rgba (G_PASTE_COLOR_ITEM (item));

        g_paste_clipboard_gdk_private_set_color (priv, rgba);

        /* Offer the colour itself plus its textual form, so it can be pasted both
         * into colour-aware apps (application/x-color) and into plain text fields. */
        GdkContentProvider *providers[] = {
            gdk_content_provider_new_typed (GDK_TYPE_RGBA, rgba),
            gdk_content_provider_new_typed (G_TYPE_STRING, g_paste_item_get_real_value (item)),
        };
        g_autoptr (GdkContentProvider) provider = gdk_content_provider_new_union (providers, G_N_ELEMENTS (providers));

        gdk_clipboard_set_content (priv->real, provider);
        return TRUE;
    }

    g_autoptr (GPtrArray) providers = g_ptr_array_new ();

    if (_G_PASTE_IS_URIS_ITEM (item))
    {
        GdkFileList *file_list = g_paste_uris_item_get_file_list (G_PASTE_URIS_ITEM (item));
        g_paste_clipboard_gdk_private_set_file_list (priv, file_list);
        g_ptr_array_add (providers, gdk_content_provider_new_typed (GDK_TYPE_FILE_LIST, file_list));
    }
    else
    {
        const gchar *real_value = g_paste_item_get_real_value (item);
        g_paste_clipboard_gdk_private_set_text (priv, real_value);
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

static gboolean
g_paste_clipboard_gdk_is_empty (const GPasteClipboardGdk *self)
{
    const GPasteClipboardGdkPrivate *priv = _g_paste_clipboard_gdk_get_instance_private (self);

    return g_paste_clipboard_content_is_empty (&priv->content);
}

static void
g_paste_clipboard_gdk_on_real_changed (GPasteClipboardGdk *self)
{
    const GPasteClipboardGdkPrivate *priv = _g_paste_clipboard_gdk_get_instance_private (self);

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

    g_debug ("%s: owner change", g_paste_clipboard_provider_target_name (priv->is_clipboard));
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (self));
}

/* GPasteClipboardProvider interface adapters */
G_PASTE_CLIPBOARD_PROVIDER_DEFINE_VFUNCS (gdk, GDK)

static void
g_paste_clipboard_gdk_dispose (GObject *object)
{
    GPasteClipboardGdkPrivate *priv = g_paste_clipboard_gdk_get_instance_private (G_PASTE_CLIPBOARD_GDK (object));

    if (priv->settings)
    {
        g_signal_handler_disconnect (priv->real, priv->c_signals[C_CHANGED]);
        g_clear_object (&priv->real);
        g_clear_object (&priv->settings);
    }

    G_OBJECT_CLASS (g_paste_clipboard_gdk_parent_class)->dispose (object);
}

static void
g_paste_clipboard_gdk_finalize (GObject *object)
{
    GPasteClipboardGdkPrivate *priv = g_paste_clipboard_gdk_get_instance_private (G_PASTE_CLIPBOARD_GDK (object));

    g_paste_clipboard_content_clear (&priv->content);

    G_OBJECT_CLASS (g_paste_clipboard_gdk_parent_class)->finalize (object);
}

static void
g_paste_clipboard_gdk_class_init (GPasteClipboardGdkClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_clipboard_gdk_dispose;
    object_class->finalize = g_paste_clipboard_gdk_finalize;
}

static void
g_paste_clipboard_gdk_init (GPasteClipboardGdk *self G_GNUC_UNUSED)
{
}

static GPasteClipboardProvider *
_g_paste_clipboard_gdk_new (GPasteSettings *settings,
                            gboolean        is_clipboard)
{
    GPasteClipboardGdk *self = g_object_new (G_PASTE_TYPE_CLIPBOARD_GDK, NULL);
    GPasteClipboardGdkPrivate *priv = g_paste_clipboard_gdk_get_instance_private (self);

    GdkDisplay *display = gdk_display_get_default ();
    priv->real = g_object_ref (is_clipboard ? gdk_display_get_clipboard (display)
                                            : gdk_display_get_primary_clipboard (display));
    priv->is_clipboard = is_clipboard;
    priv->settings = g_object_ref (settings);

    priv->c_signals[C_CHANGED] = g_signal_connect_swapped (priv->real,
                                                           "changed",
                                                           G_CALLBACK (g_paste_clipboard_gdk_on_real_changed),
                                                           self);

    return G_PASTE_CLIPBOARD_PROVIDER (self);
}

/**
 * g_paste_clipboard_gdk_new_clipboard:
 * @settings: a #GPasteSettings instance
 *
 * Create a new GDK-backed #GPasteClipboardProvider for the clipboard
 *
 * Returns: (transfer full): a newly allocated #GPasteClipboardProvider
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClipboardProvider *
g_paste_clipboard_gdk_new_clipboard (GPasteSettings *settings)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);

    return _g_paste_clipboard_gdk_new (settings, TRUE);
}

/**
 * g_paste_clipboard_gdk_new_primary:
 * @settings: a #GPasteSettings instance
 *
 * Create a new GDK-backed #GPasteClipboardProvider for the primary selection
 *
 * Returns: (transfer full): a newly allocated #GPasteClipboardProvider
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClipboardProvider *
g_paste_clipboard_gdk_new_primary (GPasteSettings *settings)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);

    return _g_paste_clipboard_gdk_new (settings, FALSE);
}
