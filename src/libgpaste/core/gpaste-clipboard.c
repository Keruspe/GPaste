/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-clipboard.h>
#include <gpaste-content-provider.h>
#include <gpaste-image-item.h>
#include <gpaste-uris-item.h>
#include <gpaste-util.h>

#include <string.h>

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
    CLIPBOARD_KIND_CLIPBOARD,
    CLIPBOARD_KIND_PRIMARY,
} ClipboardKind;

typedef struct
{
    GdkClipboard       *real;
    GdkContentProvider *content_provider;
    GPasteSettings     *settings;
    ClipboardKind       kind;

    gchar              *text;
    gchar              *image_checksum;

    guint64             c_signals[C_LAST_SIGNAL];
} GPasteClipboardPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (Clipboard, clipboard, G_TYPE_OBJECT)

enum
{
    CHANGED,

    LAST_SIGNAL
};

static guint64 signals[LAST_SIGNAL] = { 0 };

static void
g_paste_clipboard_bootstrap_finish_text (GPasteClipboard *self,
                                         const gchar     *text G_GNUC_UNUSED,
                                         gpointer         user_data)
{
    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);

    if (!priv->text)
        g_paste_clipboard_ensure_not_empty (self, user_data);
}

static void
g_paste_clipboard_bootstrap_finish_image (GPasteClipboard *self,
                                          GdkTexture      *texture,
                                          gpointer         user_data)
{
    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);

    g_object_unref (texture);
    if (!priv->image_checksum)
        g_paste_clipboard_ensure_not_empty (self, user_data);
}

/**
 * g_paste_clipboard_bootstrap:
 * @self: a #GPasteClipboard instance
 * @history: a #GPasteHistory instance
 *
 * Bootstrap a #GPasteClipboard with an initial value
 */
G_PASTE_VISIBLE void
g_paste_clipboard_bootstrap (GPasteClipboard *self,
                             GPasteHistory   *history)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (self));
    g_return_if_fail (_G_PASTE_IS_HISTORY (history));

    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);
    GdkClipboard *real = priv->real;
    GdkContentFormats *formats = gdk_clipboard_get_formats (real);

    if (gdk_content_formats_contain_gtype (formats, GDK_TYPE_FILE_LIST) ||
        gdk_content_formats_contain_gtype (formats, G_TYPE_STRING))
    {
        g_paste_clipboard_set_text (self,
                                    g_paste_clipboard_bootstrap_finish_text,
                                    history);
    }
    else if (g_paste_settings_get_images_support (priv->settings) && gdk_content_formats_contain_gtype (formats, GDK_TYPE_PIXBUF))
    {
        g_paste_clipboard_set_image (self,
                                     g_paste_clipboard_bootstrap_finish_image,
                                     history);
    }
    else
    {
        g_paste_clipboard_ensure_not_empty (self, history);
    }
}

typedef struct {
    GPasteHistory               *history;
    GPasteClipboardUpdateCallack callback;
    gpointer                     user_data;
    gboolean                     track;
} GPasteClipboardUpdateCallbackData;

typedef struct {
    GPasteHistory *history;
    GPasteItem    *item;
    const gchar   *mime;
} GPasteClipboardSpecialMimeCallbackData;

static void
g_paste_clipboard_update_finish_special_mime_read (GObject      *source_object,
                                                   GAsyncResult *res,
                                                   gpointer      user_data)
{
    g_autofree GPasteClipboardSpecialMimeCallbackData *d = user_data;
    g_autoptr (GPasteHistory) history = d->history;
    g_autoptr (GPasteItem) item = d->item;
    g_autoptr (GInputStream) stream = G_INPUT_STREAM (source_object);
    g_autoptr (GBytes) data = g_input_stream_read_bytes_finish (stream, res, NULL /* error */);

    if (data)
    {
        gsize size = 0;
        gconstpointer raw_data = g_bytes_get_data (data, &size);
        g_autofree gchar *val = g_base64_encode (raw_data, size);
        g_autofree GPasteSpecialValue *v = g_new (GPasteSpecialValue, 1);
        v->mime = d->mime;
        v->data = val;
        guint64 old_size = g_paste_item_get_size (item);
        g_paste_item_add_special_value (item, v);
        g_paste_history_refresh_item_size (history, item, old_size);
    }
}

static void
g_paste_clipboard_update_finish_special_mime (GObject      *source_object,
                                              GAsyncResult *res,
                                              gpointer      user_data)
{
    g_autofree GPasteClipboardSpecialMimeCallbackData *d = user_data;
    g_autoptr (GPasteHistory) history = d->history;
    g_autoptr (GPasteItem) item = d->item;
    GPasteClipboard *self = G_PASTE_CLIPBOARD (source_object);
    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);
    GInputStream *data = gdk_clipboard_read_finish (priv->real, res, NULL /* mime types */, NULL /* error */);

    if (data)
    {
        g_input_stream_read_bytes_async (data, G_MAXSSIZE, G_PRIORITY_DEFAULT, NULL /* cancellable */, g_paste_clipboard_update_finish_special_mime_read, d);
        d = NULL;
        history = NULL;
        item = NULL;
    }
}

static void
g_paste_clipboard_update_finish_text (GPasteClipboard *self,
                                      const gchar     *text,
                                      gpointer         user_data)
{
    g_autofree GPasteClipboardUpdateCallbackData *data = user_data;
    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);
    GPasteItem *item = NULL;
    const gchar *synchronized_text = NULL;

    g_debug ("clipboard: text ready");

    /* Did we already have some contents, or did we get some now? */
    gboolean something_in_clipboard = !!priv->text;

    /* If our contents got updated */
    if (text)
    {
        if (data->track)
            item = G_PASTE_ITEM (g_paste_text_item_new (text));

        if (g_paste_settings_get_synchronize_clipboards (priv->settings))
            synchronized_text = text;
    }

    GdkContentFormats *formats = gdk_clipboard_get_formats (priv->real);

    if (item && g_paste_settings_get_rich_text_support (priv->settings))
    {
        for (GPasteSpecialMime m = G_PASTE_SPECIAL_MIME_FIRST; m < G_PASTE_SPECIAL_MIME_LAST; ++m)
        {
            const gchar *mime = g_paste_special_mime_get (m);
            const gchar *mimes[] = { mime, NULL };

            if (gdk_content_formats_contain_mime_type (formats, mime))
            {
                GPasteClipboardSpecialMimeCallbackData *d = g_new (GPasteClipboardSpecialMimeCallbackData, 1);
                d->history = g_object_ref (data->history);
                d->item = g_object_ref (item);
                d->mime = mime;
                gdk_clipboard_read_async (priv->real, mimes, G_PRIORITY_DEFAULT, NULL /* Cancellable */, g_paste_clipboard_update_finish_special_mime, d);
            }
        }
    }

    data->callback (self, item, synchronized_text, something_in_clipboard, data->user_data);
}

static const gchar *
_g_paste_clipboard_private_target_name (const GPasteClipboardPrivate *priv)
{
    switch (priv->kind)
    {
        case CLIPBOARD_KIND_CLIPBOARD:
            return "CLIPBOARD";
        case CLIPBOARD_KIND_PRIMARY:
            return "PRIMARY";
        default:
            return "UNKNOWN"; // unreachable
    }
}

static void
g_paste_clipboard_private_set_text (GPasteClipboardPrivate *priv,
                                    const gchar            *text)
{
    g_free (priv->text);
    g_free (priv->image_checksum);

    g_debug("%s: set text", _g_paste_clipboard_private_target_name (priv));

    priv->text = g_strdup (text);
    priv->image_checksum = NULL;
}

static void
g_paste_clipboard_update_finish_uris (GPasteClipboard *self,
                                      const GSList    *files,
                                      gpointer         user_data)
{
    g_autofree GPasteClipboardUpdateCallbackData *data = user_data;
    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);
    GPasteItem *item = NULL;

    g_debug ("clipboard: uris ready");

    /* Did we already have some contents, or did we get some now? */
    gboolean something_in_clipboard = !!priv->text;

    /* If our contents got updated */
    if (files && data->track)
        item = G_PASTE_ITEM (g_paste_uris_item_new (files));

    g_paste_clipboard_private_set_text (priv, g_paste_item_get_value (item));

    data->callback (self, item, NULL, something_in_clipboard, data->user_data);
}

static void
g_paste_clipboard_update_finish_image (GPasteClipboard *self,
                                       GdkTexture      *texture,
                                       gpointer         user_data)
{
    g_autofree GPasteClipboardUpdateCallbackData *data = user_data;
    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);
    GPasteItem *item = NULL;

    /* Did we already have some contents, or did we get some now? */
    gboolean something_in_clipboard = !!priv->image_checksum;

    if (texture)
    {
        if (data->track)
            item = G_PASTE_ITEM (g_paste_image_item_new (texture));
        else
            g_object_unref (texture);
    }

    data->callback (self, item, NULL, something_in_clipboard, data->user_data);
}

typedef struct {
    GPasteClipboard            *self;
    GPasteClipboardUrisCallback callback;
    gpointer                    user_data;
} GPasteClipboardUrisCallbackData;

static void
g_paste_clipboard_on_uris_ready (GObject      *source_object,
                                 GAsyncResult *res,
                                 gpointer      user_data)
{
    const GValue *files_value = gdk_clipboard_read_value_finish (GDK_CLIPBOARD (source_object), res, NULL /* FIXME: error */);
    const GSList *files = (files_value) ? g_value_get_boxed (files_value) : NULL;
    g_autofree GPasteClipboardUrisCallbackData *data = user_data;
    GPasteClipboard *self = data->self;

    if (data->callback)
        data->callback (self, files, data->user_data);
}

static void
g_paste_clipboard_set_uris (GPasteClipboard            *self,
                            GPasteClipboardUrisCallback callback,
                            gpointer                    user_data)
{
    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);
    GPasteClipboardUrisCallbackData *data = g_new0 (GPasteClipboardUrisCallbackData, 1);

    data->self = self;
    data->callback = callback;
    data->user_data = user_data;

    gdk_clipboard_read_value_async (priv->real,
                                    GDK_TYPE_FILE_LIST,
                                    G_PRIORITY_DEFAULT,
                                    NULL, /* cancellable */
                                    g_paste_clipboard_on_uris_ready,
                                    data);
}

/**
 * g_paste_clipboard_update:
 * @self: a #GPasteClipboard instance
 * @history: a #GPasteHistory instance
 * @callback: (scope async): the callback to be called when data is received
 * @user_data: user data to pass to @callback
 *
 * Update a #GPasteClipboard with the value from the system clipboard
 */
G_PASTE_VISIBLE void
g_paste_clipboard_update (GPasteClipboard             *self,
                          GPasteHistory               *history,
                          GPasteClipboardUpdateCallack callback,
                          gpointer                     user_data)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (self));
    g_return_if_fail (_G_PASTE_IS_HISTORY (history));
    g_return_if_fail (callback);

    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);
    GdkClipboard *real = priv->real;
    GPasteSettings *settings = priv->settings;
    GdkContentFormats *formats = gdk_clipboard_get_formats (real);
    g_autofree GPasteClipboardUpdateCallbackData *data = g_new0 (GPasteClipboardUpdateCallbackData, 1);

    data->history = history;
    data->callback = callback;
    data->user_data = user_data;
    data->track = (g_paste_settings_get_track_changes (settings) &&
                      (priv->kind == CLIPBOARD_KIND_CLIPBOARD ||             // We're not primary
                       g_paste_settings_get_primary_to_history (settings) ||     // Or we asked that primary affects clipboard
                       g_paste_settings_get_synchronize_clipboards (settings))); // Or primary and clipboards are synchronized hence primary will affect history through clipboard

    if (g_paste_settings_get_images_support (settings) && gdk_content_formats_contain_gtype (formats, GDK_TYPE_TEXTURE))
    {
        g_paste_clipboard_set_image (self,
                                     g_paste_clipboard_update_finish_image,
                                     data);
        data = NULL;
    }
    else if (gdk_content_formats_contain_gtype (formats, GDK_TYPE_FILE_LIST))
    {
        g_paste_clipboard_set_uris (self,
                                    g_paste_clipboard_update_finish_uris,
                                    data);
        data = NULL;
    }
    else if (gdk_content_formats_contain_gtype (formats, G_TYPE_STRING))
    {
        g_paste_clipboard_set_text (self,
                                    g_paste_clipboard_update_finish_text,
                                    data);
        data = NULL;
    }
    else
    {
        g_debug ("clipboard: no target ready");
    }
}

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

    return priv->kind == CLIPBOARD_KIND_CLIPBOARD;
}

/**
 * g_paste_clipboard_get_real:
 * @self: a #GPasteClipboard instance
 *
 * Get the GdkClipboard linked to the #GPasteClipboard
 *
 * Returns: (transfer none): the GdkClipboard used in the #GPasteClipboard
 */
G_PASTE_VISIBLE GdkClipboard *
g_paste_clipboard_get_real (const GPasteClipboard *self)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIPBOARD (self), NULL);

    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);

    return priv->real;
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

    return priv->text;
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
    g_autofree gchar *text = gdk_clipboard_read_text_finish (GDK_CLIPBOARD (source_object), res, NULL /* FIXME: error */);
    g_autofree GPasteClipboardTextCallbackData *data = user_data;
    GPasteClipboard *self = data->self;

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
        length > g_paste_settings_get_max_text_item_size (settings) ||
        !strlen (stripped))
    {
        if (data->callback)
            data->callback (self, NULL, data->user_data);
        return;
    }
    if (priv->text && g_paste_str_equal (priv->text, to_add))
    {
        if (data->callback)
            data->callback (self, NULL, data->user_data);
        return;
    }

    /* FIXME: might need tweaking for sync primary and clipboard? */
    if (trim_items &&
        priv->kind == CLIPBOARD_KIND_CLIPBOARD &&
        !g_paste_str_equal (text, stripped))
            g_paste_clipboard_select_text (self, stripped);
    else
        g_paste_clipboard_private_set_text (priv, to_add);

    if (data->callback)
        data->callback (self, priv->text, data->user_data);
}

/**
 * g_paste_clipboard_set_text:
 * @self: a #GPasteClipboard instance
 * @callback: (scope async): the callback to be called when text is received
 * @user_data: user data to pass to @callback
 *
 * Put the text from the intern GtkClipboard in the #GPasteClipboard
 */
G_PASTE_VISIBLE void
g_paste_clipboard_set_text (GPasteClipboard            *self,
                            GPasteClipboardTextCallback callback,
                            gpointer                    user_data)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (self));

    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);
    GPasteClipboardTextCallbackData *data = g_new0 (GPasteClipboardTextCallbackData, 1);

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
 * Put the text into the #GPasteClipbaord and the intern GtkClipboard
 */
G_PASTE_VISIBLE void
g_paste_clipboard_select_text (GPasteClipboard *self,
                               const gchar     *text)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (self));
    g_return_if_fail (text);
    g_return_if_fail (g_utf8_validate (text, -1, NULL));

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);

    g_debug("%s: select text", _g_paste_clipboard_private_target_name (priv));

    /* Avoid cycling twice as gtk_clipboard_set_text might make the clipboards manager react */
    g_paste_clipboard_private_set_text (priv, text);
    gdk_clipboard_set_text (priv->real, text);
}

static void
g_paste_clipboard_sync_text_ready (GObject      *source_object,
                                   GAsyncResult *res,
                                   gpointer      user_data)
{
    g_autofree gchar *text = gdk_clipboard_read_text_finish (GDK_CLIPBOARD (source_object), res, NULL /* FIXME: error */);

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

    gdk_clipboard_read_text_async (priv->real, NULL, g_paste_clipboard_sync_text_ready, other);
}

#if 0
static guchar *
copy_str_as_uchars (const gchar *str,
                    guint64      length)
{
    guchar *data = g_new (guchar, length);
    for (guint64 i = 0; i < length; ++i)
        data[i] = (guchar) str[i];
    return data;
}

static void
_get_clipboard_data_from_special_atom (GtkSelectionData *selection_data,
                                       const GPasteItem *item,
                                       GPasteSpecialAtom atom)
{
    if (atom >= G_PASTE_SPECIAL_ATOM_FIRST && atom < G_PASTE_SPECIAL_ATOM_LAST)
    {
        g_autofree guchar *data = NULL;
        guint64 length = 0;
        const gchar *str = g_paste_item_get_special_value (item, atom);

        if (str)
        {
            data = g_base64_decode (str, &length);
        }
        else
        {
            str = g_paste_item_get_value (item);
            length = strlen (str);
            data = copy_str_as_uchars (str, length);
        }

        gtk_selection_data_set (selection_data, g_paste_special_atom_get (atom), 8, data, length);
    }
}

static void
g_paste_clipboard_get_clipboard_data (GtkClipboard     *clipboard G_GNUC_UNUSED,
                                      GtkSelectionData *selection_data,
                                      guint32           info      G_GNUC_UNUSED,
                                      gpointer          user_data_or_owner)
{
    g_return_if_fail (_G_PASTE_IS_ITEM (user_data_or_owner));

    GPasteItem *item = G_PASTE_ITEM (user_data_or_owner);

    GdkAtom target = gtk_selection_data_get_target (selection_data);
    GdkAtom targets[1] = { target };

    if (_G_PASTE_IS_IMAGE_ITEM (item))
    {
        if (gtk_targets_include_image (targets, 1, TRUE))
            gtk_selection_data_set_pixbuf (selection_data, g_paste_image_item_get_image (G_PASTE_IMAGE_ITEM (item)));
        return;
    }
    else if (_G_PASTE_IS_URIS_ITEM (item))
    {
        if (gtk_targets_include_uri (targets, 1))
        {
            const gchar * const *uris = g_paste_uris_item_get_uris (G_PASTE_URIS_ITEM (item));

            gtk_selection_data_set_uris (selection_data, (GStrv) uris);
            return;
        }
    }

    for (GPasteSpecialAtom a = G_PASTE_SPECIAL_ATOM_FIRST; a < G_PASTE_SPECIAL_ATOM_LAST; ++a)
    {
        if (target == g_paste_special_atom_get (a))
        {
            _get_clipboard_data_from_special_atom (selection_data, item, a);
            return;
        }
    }

    /* The content is requested as text */
    if (gtk_targets_include_text (targets, 1))
        gtk_selection_data_set_text (selection_data, g_paste_item_get_real_value (item), -1);
}
#endif

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
    if (!priv->text && !priv->image_checksum)
        return;

    g_debug("%s: clear", _g_paste_clipboard_private_target_name (priv));

    g_clear_pointer (&priv->text, g_free);
    g_clear_pointer (&priv->image_checksum, g_free);

    gdk_clipboard_set_content (priv->real, NULL);
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

    return priv->image_checksum;
}

static void
g_paste_clipboard_private_set_image_checksum (GPasteClipboardPrivate *priv,
                                              const gchar            *image_checksum)
{
    g_free (priv->text);
    g_free (priv->image_checksum);

    priv->text = NULL;
    priv->image_checksum = g_strdup (image_checksum);
}

static void
g_paste_clipboard_private_select_image (GPasteClipboardPrivate *priv,
                                        GdkTexture             *texture,
                                        const gchar            *checksum)
{
    g_return_if_fail (GDK_IS_TEXTURE (texture));

    GdkClipboard *real = priv->real;

    g_debug("%s: select image", _g_paste_clipboard_private_target_name (priv));

    g_paste_clipboard_private_set_image_checksum (priv, checksum);
    gdk_clipboard_set_texture (real, texture);
}

typedef struct {
    GPasteClipboard             *self;
    GPasteClipboardImageCallback callback;
    gpointer                     user_data;
} GPasteClipboardImageCallbackData;

static void
g_paste_clipboard_on_image_ready (GObject      *source_object,
                                  GAsyncResult *res,
                                  gpointer      user_data)
{
    g_autofree GPasteClipboardImageCallbackData *data = user_data;
    GPasteClipboard *self = data->self;
    g_autoptr (GdkTexture) texture = gdk_clipboard_read_texture_finish (GDK_CLIPBOARD (source_object), res, NULL /* error */);

    if (!texture)
    {
        if (data->callback)
            data->callback (self, NULL, data->user_data);
        return;
    }

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);
    g_autofree gchar *checksum = g_paste_util_compute_checksum (texture);

    if (g_paste_str_equal (checksum, priv->image_checksum))
        g_clear_object (&texture);
    else
        g_paste_clipboard_private_select_image (priv, g_object_ref (texture), checksum);

    if (data->callback)
    {
        data->callback (self, texture, data->user_data);
        texture = NULL; // Don't autofree it, we just passed its ownership to the callback
    }
}

/**
 * g_paste_clipboard_set_image:
 * @self: a #GPasteClipboard instance
 * @callback: (scope async): the callback to be called when text is received
 * @user_data: user data to pass to @callback
 *
 * Put the image from the intern GtkClipboard in the #GPasteClipboard
 */
G_PASTE_VISIBLE void
g_paste_clipboard_set_image (GPasteClipboard             *self,
                             GPasteClipboardImageCallback callback,
                             gpointer                     user_data)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (self));

    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);
    GPasteClipboardImageCallbackData *data = g_new0 (GPasteClipboardImageCallbackData, 1);

    data->self = self;
    data->callback = callback;
    data->user_data = user_data;

    gdk_clipboard_read_texture_async (priv->real,
                                      NULL, /* cancellable */
                                      g_paste_clipboard_on_image_ready,
                                      data);
}

/**
 * g_paste_clipboard_select_item:
 * @self: a #GPasteClipboard instance
 * @item: the item to select
 *
 * Put the value of the item into the #GPasteClipbaord and the intern GtkClipboard
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

    g_debug("%s: select item", _g_paste_clipboard_private_target_name (priv));

    /* FIXME
    for (const GSList *sv = g_paste_item_get_special_values (item); sv; sv = sv->next)
    {
        const GPasteSpecialValue *v = sv->data;
        const gchar *mime = gdk_intern_mime_type (v->mime);
        if (mime)
            gdk_content_formats_builder_add_mime_type (formats_builder, mime);
    }
    */

    if (_G_PASTE_IS_IMAGE_ITEM (item))
        g_paste_clipboard_private_set_image_checksum (priv, g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (item)));
    else
        g_paste_clipboard_private_set_text (priv, g_paste_item_get_real_value (item));

    g_paste_content_provider_set_item (G_PASTE_CONTENT_PROVIDER (priv->content_provider), item);
    gdk_clipboard_set_content (priv->real, priv->content_provider);

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

    const GList *hist = g_paste_history_get_history (history);

    if (hist)
    {
        GPasteItem *item = hist->data;

        if (!g_paste_clipboard_select_item (self, item))
            g_paste_history_remove (history, 0);
    }
}

static void
g_paste_clipboard_changed (GdkClipboard *clipboard G_GNUC_UNUSED,
                           gpointer      user_data)
{
    GPasteClipboard *self = user_data;
    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);

    g_debug("%s: changed", _g_paste_clipboard_private_target_name (priv));

    g_signal_emit (self,
		   signals[CHANGED],
                   0, /* detail */
                   NULL);
}

static void
g_paste_clipboard_dispose (GObject *object)
{
    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (G_PASTE_CLIPBOARD (object));

    if (priv->settings)
    {
        g_signal_handler_disconnect (priv->real, priv->c_signals[C_CHANGED]);
        g_clear_object (&priv->settings);
    }
    g_clear_object (&priv->content_provider);

    G_OBJECT_CLASS (g_paste_clipboard_parent_class)->dispose (object);
}

static void
g_paste_clipboard_finalize (GObject *object)
{
    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (G_PASTE_CLIPBOARD (object));

    g_free (priv->text);
    g_free (priv->image_checksum);

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
     * The "changed" signal is emitted when GPaste receives an
     * event that indicates that the ownership of the selection
     * associated with @clipboard has changed.
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
_g_paste_clipboard_new (GPasteSettings        *settings,
                        GPasteContentProvider *content_provider,
                        GdkClipboard          *real,
                        ClipboardKind          kind)
{
    GPasteClipboard *self = g_object_new (G_PASTE_TYPE_CLIPBOARD, NULL);
    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);

    priv->real = real;
    priv->settings = g_object_ref (settings);
    priv->content_provider = GDK_CONTENT_PROVIDER (g_object_ref (content_provider));
    priv->kind = kind;
    priv->c_signals[C_CHANGED] = g_signal_connect (real,
                                                   "changed",
                                                   G_CALLBACK (g_paste_clipboard_changed),
                                                   self);

    return self;
}

/**
 * g_paste_clipboard_new_clipboard:
 * @settings: a #GPasteSettings instance
 * @content_provider: a #GPasteContentProvider instance
 *
 * Create a new instance of #GPasteClipboard
 *
 * Returns: a newly allocated #GPasteClipboard
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClipboard *
g_paste_clipboard_new_clipboard (GPasteSettings        *settings,
                                 GPasteContentProvider *content_provider)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (_G_PASTE_IS_CONTENT_PROVIDER (content_provider), NULL);

    return _g_paste_clipboard_new (settings, content_provider, gdk_display_get_clipboard (gdk_display_get_default ()), CLIPBOARD_KIND_CLIPBOARD);
}

/**
 * g_paste_clipboard_new_primary:
 * @settings: a #GPasteSettings instance
 * @content_provider: a #GPasteContentProvider instance
 *
 * Create a new instance of #GPasteClipboard
 *
 * Returns: a newly allocated #GPasteClipboard
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClipboard *
g_paste_clipboard_new_primary (GPasteSettings        *settings,
                               GPasteContentProvider *content_provider)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (_G_PASTE_IS_CONTENT_PROVIDER (content_provider), NULL);

    return _g_paste_clipboard_new (settings, content_provider, gdk_display_get_primary_clipboard (gdk_display_get_default ()), CLIPBOARD_KIND_PRIMARY);
}
