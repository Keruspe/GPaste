/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk3.h>

#include <string.h>

#include <gpaste-clipboard.h>
#include <gpaste-image-item.h>
#include <gpaste-uris-item.h>

struct _GPasteClipboard
{
    GObject parent_instance;
};

enum
{
    C_OWNER_CHANGE,

    C_LAST_SIGNAL
};

typedef struct
{
    GtkClipboard   *real;
    GPasteSettings *settings;
    gchar          *text;
    gchar          *image_checksum;

    guint64         c_signals[C_LAST_SIGNAL];
} GPasteClipboardPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (Clipboard, clipboard, G_TYPE_OBJECT)

enum
{
    OWNER_CHANGE,

    LAST_SIGNAL
};

static guint64 signals[LAST_SIGNAL] = { 0 };

static void
g_paste_clipboard_bootstrap_finish_text (GPasteClipboard *self,
                                         const gchar     *text G_GNUC_UNUSED,
                                         gpointer         user_data)
{
    g_paste_clipboard_ensure_not_empty (self, user_data);
}

static void
g_paste_clipboard_bootstrap_finish_image (GPasteClipboard *self,
                                          GdkPixbuf       *image,
                                          gpointer         user_data)
{
    g_object_unref (image);
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
    GtkClipboard *real = priv->real;

    if (gtk_clipboard_wait_is_uris_available (real) ||
        gtk_clipboard_wait_is_text_available (real))
    {
        g_paste_clipboard_set_text (self,
                                    g_paste_clipboard_bootstrap_finish_text,
                                    history);
    }
    else if (g_paste_settings_get_images_support (priv->settings) && gtk_clipboard_wait_is_image_available (real))
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

    return gtk_clipboard_get_selection (priv->real) == GDK_SELECTION_CLIPBOARD;
}

/**
 * g_paste_clipboard_get_real:
 * @self: a #GPasteClipboard instance
 *
 * Get the GtkClipboard linked to the #GPasteClipboard
 *
 * Returns: (transfer none): the GtkClipboard used in the #GPasteClipboard
 */
G_PASTE_VISIBLE GtkClipboard *
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

static const gchar *
_g_paste_clipboard_private_target_name (const GPasteClipboardPrivate *priv)
{
    GdkAtom target = gtk_clipboard_get_selection (priv->real);

    if (target == GDK_SELECTION_CLIPBOARD)
        return "CLIPBOARD";
    else if (target == GDK_SELECTION_PRIMARY)
        return "PRIMARY";
    else
        return "UNKNOWN";
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

typedef struct {
    GPasteClipboard            *self;
    GPasteClipboardTextCallback callback;
    gpointer                    user_data;
} GPasteClipboardTextCallbackData;

static void
g_paste_clipboard_on_text_ready (GtkClipboard *clipboard G_GNUC_UNUSED,
                                 const gchar  *text,
                                 gpointer      user_data)
{
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
        length > g_paste_settings_get_max_text_item_size (settings))
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

    if (trim_items &&
        gtk_clipboard_get_selection (priv->real) == GDK_SELECTION_CLIPBOARD &&
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
    GPasteClipboardTextCallbackData *data = g_new (GPasteClipboardTextCallbackData, 1);

    data->self = self;
    data->callback = callback;
    data->user_data = user_data;

    gtk_clipboard_request_text (priv->real,
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

    /* Avoid cycling twice as gtk_clipboard_set_text will make the clipboards manager react */
    g_paste_clipboard_private_set_text (priv, text);
    gtk_clipboard_set_text (priv->real, text, -1);
}

static void
g_paste_clipboard_sync_ready (GtkClipboard *clipboard G_GNUC_UNUSED,
                              const gchar  *text,
                              gpointer      user_data)
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

    gtk_clipboard_request_text (priv->real, g_paste_clipboard_sync_ready, other);
}

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

static void
g_paste_clipboard_clear_clipboard_data (GtkClipboard *clipboard G_GNUC_UNUSED,
                                        gpointer      user_data_or_owner)
{
    g_object_unref (user_data_or_owner);
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
    if (!priv->text && !priv->image_checksum)
        return;

    g_debug("%s: clear", _g_paste_clipboard_private_target_name (priv));

    g_clear_pointer (&priv->text, g_free);
    g_clear_pointer (&priv->image_checksum, g_free);

    gtk_clipboard_clear (priv->real);
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

    g_debug("%s: store", _g_paste_clipboard_private_target_name (priv));

    gtk_clipboard_store (priv->real);
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
                                        GdkPixbuf              *image,
                                        const gchar            *checksum)
{
    g_return_if_fail (GDK_IS_PIXBUF (image));

    GtkClipboard *real = priv->real;

    g_debug("%s: select image", _g_paste_clipboard_private_target_name (priv));

    g_paste_clipboard_private_set_image_checksum (priv, checksum);
    gtk_clipboard_set_image (real, image);
}

typedef struct {
    GPasteClipboard             *self;
    GPasteClipboardImageCallback callback;
    gpointer                     user_data;
} GPasteClipboardImageCallbackData;

static void
g_paste_clipboard_on_image_ready (GtkClipboard *clipboard G_GNUC_UNUSED,
                                  GdkPixbuf    *image,
                                  gpointer      user_data)
{
    g_autofree GPasteClipboardImageCallbackData *data = user_data;
    GPasteClipboard *self = data->self;

    if (!image)
    {
        if (data->callback)
            data->callback (self, NULL, data->user_data);
        return;
    }

    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);
    g_autofree gchar *checksum = g_paste_gtk_util_compute_checksum (image);

    if (g_paste_str_equal (checksum, priv->image_checksum))
        image = NULL;
    else
        g_paste_clipboard_private_select_image (priv, image, checksum);

    if (data->callback)
        data->callback (self, image, data->user_data);
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
    GPasteClipboardImageCallbackData *data = g_new (GPasteClipboardImageCallbackData, 1);

    data->self = self;
    data->callback = callback;
    data->user_data = user_data;

    gtk_clipboard_request_image (priv->real,
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
    GtkClipboard *real = priv->real;
    g_autoptr (GtkTargetList) target_list = gtk_target_list_new (NULL, 0);

    g_debug("%s: select item", _g_paste_clipboard_private_target_name (priv));

    if (_G_PASTE_IS_IMAGE_ITEM (item))
    {
        gtk_target_list_add_image_targets (target_list, 0, FALSE);
        g_paste_clipboard_private_set_image_checksum (priv, g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (item)));
    }
    else
    {
        gtk_target_list_add_text_targets (target_list, 0);
        if (_G_PASTE_IS_URIS_ITEM (item))
            gtk_target_list_add_uri_targets (target_list, 0);
        g_paste_clipboard_private_set_text (priv, g_paste_item_get_real_value (item));
    }

    for (const GSList *sv = g_paste_item_get_special_values (item); sv; sv = sv->next)
    {
        const GPasteSpecialValue *v = sv->data;
        gtk_target_list_add (target_list, g_paste_special_atom_get (v->mime), 0, 0);
    }

    gint32 n_targets;
    GtkTargetEntry *targets = gtk_target_table_new_from_list (target_list, &n_targets);
    gtk_clipboard_set_with_owner (real,
                                  targets,
                                  n_targets,
                                  g_paste_clipboard_get_clipboard_data,
                                  g_paste_clipboard_clear_clipboard_data,
                                  g_object_ref ((GObject *) item));
    gtk_target_table_free (targets, n_targets);

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

    if (priv->text || priv->image_checksum)
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
g_paste_clipboard_owner_change (GtkClipboard        *clipboard G_GNUC_UNUSED,
                                GdkEventOwnerChange *event,
                                gpointer             user_data)
{
    GPasteClipboard *self = user_data;
    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);

    g_debug("%s: owner change", _g_paste_clipboard_private_target_name (priv));

    g_signal_emit (self,
		   signals[OWNER_CHANGE],
                   0, /* detail */
                   event,
                   NULL);
}

static void
g_paste_clipboard_fake_event_finish_text (GtkClipboard *clipboard G_GNUC_UNUSED,
                                          const gchar  *text,
                                          gpointer      user_data)
{
    GPasteClipboard *self = user_data;
    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);

    if (!g_paste_str_equal (text, priv->text))
        g_paste_clipboard_owner_change (NULL, NULL, self);
}

static void
g_paste_clipboard_fake_event_finish_image (GtkClipboard *clipboard G_GNUC_UNUSED,
                                           GdkPixbuf    *image,
                                           gpointer      user_data)
{
    GPasteClipboard *self = user_data;
    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);
    g_autofree gchar *checksum = g_paste_gtk_util_compute_checksum (image);

    if (!g_paste_str_equal (checksum, priv->image_checksum))
        g_paste_clipboard_owner_change (NULL, NULL, self);

    g_object_unref (image);
}

static gboolean
g_paste_clipboard_fake_event (gpointer user_data)
{
    GPasteClipboard *self = user_data;
    const GPasteClipboardPrivate *priv = _g_paste_clipboard_get_instance_private (self);

    if (priv->text)
        gtk_clipboard_request_text (priv->real, g_paste_clipboard_fake_event_finish_text, self);
    else if (priv->image_checksum)
        gtk_clipboard_request_image (priv->real, g_paste_clipboard_fake_event_finish_image, self);
    else
        g_paste_clipboard_owner_change (NULL, NULL, self);

    return G_SOURCE_CONTINUE;
}

static void
g_paste_clipboard_dispose (GObject *object)
{
    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (G_PASTE_CLIPBOARD (object));

    if (priv->settings)
    {
        g_signal_handler_disconnect (priv->real, priv->c_signals[C_OWNER_CHANGE]);
        g_clear_object (&priv->settings);
    }

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
     * GPasteClipboard::owner-change:
     * @clipboard: the object on which the signal was emitted
     * @event: (type Gdk.EventOwnerChange): the @GdkEventOwnerChange event
     *
     * The "owner-change" signal is emitted when GPaste receives an
     * event that indicates that the ownership of the selection
     * associated with @clipboard has changed.
     */
    signals[OWNER_CHANGE] = g_signal_new ("owner-change",
                                          G_PASTE_TYPE_CLIPBOARD,
                                          G_SIGNAL_RUN_FIRST,
                                          0,    /* class offset     */
                                          NULL, /* accumulator      */
                                          NULL, /* accumulator data */
                                          g_cclosure_marshal_VOID__BOXED,
                                          G_TYPE_NONE,
                                          1,
                                          GDK_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
}

static void
g_paste_clipboard_init (GPasteClipboard *self G_GNUC_UNUSED)
{
}

static GPasteClipboard *
_g_paste_clipboard_new (GPasteSettings *settings,
                        GdkAtom         target)
{
    GPasteClipboard *self = g_object_new (G_PASTE_TYPE_CLIPBOARD, NULL);
    GPasteClipboardPrivate *priv = g_paste_clipboard_get_instance_private (self);
    GtkClipboard *real = priv->real = gtk_clipboard_get (target);

    priv->settings = g_object_ref (settings);
    priv->c_signals[C_OWNER_CHANGE] = g_signal_connect (real,
                                                        "owner-change",
                                                        G_CALLBACK (g_paste_clipboard_owner_change),
                                                        self);

    if (!gdk_display_request_selection_notification (gdk_display_get_default (), target))
    {
        g_warning ("Selection notification not supported, using active poll");
        g_source_set_name_by_id (g_timeout_add_seconds (1, g_paste_clipboard_fake_event, self), "[GPaste] clipboard fake events");
    }

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

    return _g_paste_clipboard_new (settings, GDK_SELECTION_CLIPBOARD);
}

/**
 * g_paste_clipboard_new_primary:
 * @settings: a #GPasteSettings instance
 *
 * Create a new instance of #GPasteClipboard
 *
 * Returns: a newly allocated #GPasteClipboard
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClipboard *
g_paste_clipboard_new_primary (GPasteSettings *settings)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);

    return _g_paste_clipboard_new (settings, GDK_SELECTION_PRIMARY);
}
