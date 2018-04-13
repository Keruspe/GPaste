/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-clipboards-manager.h>
#include <gpaste-image-item.h>
#include <gpaste-uris-item.h>

struct _GPasteClipboardsManager
{
    GObject parent_instance;
};

enum
{
    C_CLIP_OWNER_CHANGE,

    C_CLIP_LAST_SIGNAL
};

typedef struct
{
    GPasteClipboard *clipboard;
    guint64          c_signals[C_CLIP_LAST_SIGNAL];
} _Clipboard;

enum
{
    C_SELECTED,

    C_LAST_SIGNAL
};

typedef struct
{
    GSList         *clipboards;
    GPasteHistory  *history;
    GPasteSettings *settings;

    guint64         c_signals[C_LAST_SIGNAL];
} GPasteClipboardsManagerPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (ClipboardsManager, clipboards_manager, G_TYPE_OBJECT)

/**
 * g_paste_clipboards_manager_add_clipboard:
 * @self: a #GPasteClipboardsManager instance
 * @clipboard: (transfer none): the GPasteClipboard to add
 *
 * Add a #GPasteClipboard to the #GPasteClipboardsManager
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_add_clipboard (GPasteClipboardsManager *self,
                                          GPasteClipboard         *clipboard)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARDS_MANAGER (self));
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD (clipboard));

    GPasteClipboardsManagerPrivate *priv = g_paste_clipboards_manager_get_instance_private (self);
    _Clipboard *clip = g_new0 (_Clipboard, 1);

    clip->clipboard = g_object_ref (clipboard);

    priv->clipboards = g_slist_prepend (priv->clipboards, clip);
    g_paste_clipboard_bootstrap (clipboard, priv->history);
}

/**
 * g_paste_clipboards_manager_sync_from_to:
 * @self: a #GPasteClipboardsManager instance
 * @from_clipboard: whether we sync from clipboard or to clipboard
 *
 * Sync a clipboard into another
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_sync_from_to (GPasteClipboardsManager *self,
                                         gboolean                 from_clipboard)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARDS_MANAGER (self));

    const GPasteClipboardsManagerPrivate *priv = _g_paste_clipboards_manager_get_instance_private (self);
    GPasteClipboard *_from = NULL;
    GPasteClipboard *_to = NULL;

    g_debug ("clipboards-manager: sync_from_to");

    for (GSList *clipboard = priv->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        _Clipboard *_clip = clipboard->data;
        GPasteClipboard *clip = _clip->clipboard;

        if (g_paste_clipboard_is_clipboard (clip) == from_clipboard)
            _from = clip;
        else
            _to = clip;
    }

    if (_from && _to)
        g_paste_clipboard_sync_text (_from, _to);
}

static void
g_paste_clipboards_manager_notify_finish (GPasteClipboardsManagerPrivate *priv,
                                          GPasteClipboard                *clipboard,
                                          GPasteItem                     *item,
                                          const gchar                    *synchronized_text,
                                          gboolean                        something_in_clipboard)
{
    GPasteHistory *history = priv->history;

    g_debug ("clipboards-manager: notify finish");

    if (item)
        g_paste_history_add (history, item);

    if (!something_in_clipboard)
        g_paste_clipboard_ensure_not_empty (clipboard, history);

    if (synchronized_text)
    {
        g_debug ("clipboards-manager: synchronizing clipboards");

        for (GSList *_clipboard = priv->clipboards; _clipboard; _clipboard = g_slist_next (_clipboard))
        {
            _Clipboard *_clip = _clipboard->data;
            GPasteClipboard *clip = _clip->clipboard;

            if (clipboard == clip)
                continue;

            const gchar *text = g_paste_clipboard_get_text (clip);

            if (!text || !g_paste_str_equal (text, synchronized_text))
                g_paste_clipboard_select_text (clip, synchronized_text);
        }
    }
}

typedef struct {
    GPasteClipboardsManagerPrivate *priv;
    GPasteClipboard                *clip;
    gboolean                        track;
    gboolean                        uris_available;
    gboolean                        fallback;
    gboolean                        special_atom_available[G_PASTE_SPECIAL_ATOM_LAST];
} GPasteClipboardsManagerCallbackData;

typedef struct {
    GPasteHistory    *history;
    GPasteItem       *item;
    GPasteSpecialAtom atom;
} GPasteSpecialAtomCallbackData;

// TODO: move that to g_paste_clipboard ?
static void
special_contents_received (GtkClipboard     *clipboard G_GNUC_UNUSED,
                           GtkSelectionData *selection_data,
                           gpointer          data)
{
    g_autofree GPasteSpecialAtomCallbackData *d = data;
    g_autoptr (GPasteHistory) history = d->history;
    g_autoptr (GPasteItem) item = d->item;
    gint length;
    const guchar *raw_val = gtk_selection_data_get_data_with_length (selection_data, &length);

    if (raw_val)
    {
        g_autofree gchar *val = g_base64_encode (raw_val, length);
        g_autofree GPasteSpecialValue *v = g_new (GPasteSpecialValue, 1);
        v->mime = d->atom;
        v->data = val;
        guint64 old_size = g_paste_item_get_size (item);
        g_paste_item_add_special_value (item, v);
        g_paste_history_refresh_item_size (history, item, old_size);
    }
}

static void
g_paste_clipboards_manager_text_ready (GPasteClipboard *clipboard,
                                       const gchar     *text,
                                       gpointer         user_data)
{
    g_autofree GPasteClipboardsManagerCallbackData *data = user_data;
    GPasteClipboardsManagerPrivate *priv = data->priv;
    GPasteItem *item = NULL;
    const gchar *synchronized_text = NULL;

    g_debug ("clipboards-manager: text ready");

    /* Did we already have some contents, or did we get some now? */
    gboolean something_in_clipboard = !!g_paste_clipboard_get_text (clipboard);

    /* If our contents got updated */
    if (text)
    {
        if (data->track)
        {
            if (data->uris_available)
                item = G_PASTE_ITEM (g_paste_uris_item_new (text));
            else
                item = G_PASTE_ITEM (g_paste_text_item_new (text));
        }

        if (g_paste_settings_get_synchronize_clipboards (priv->settings))
            synchronized_text = text;
    }
    else if (data->fallback)
    {
        g_debug ("clipboards-manager: no target ready and text fallback failed");

        /* We tried to get some text as fallback (no target advertised) but didn't get any */
        g_paste_clipboard_clear (data->clip);
        g_paste_clipboard_ensure_not_empty (data->clip, priv->history);

        return;
    }

    if (item && g_paste_settings_get_rich_text_support (priv->settings))
    {
        for (GPasteSpecialAtom atom = G_PASTE_SPECIAL_ATOM_FIRST; atom < G_PASTE_SPECIAL_ATOM_LAST; ++atom)
        {
            if (data->special_atom_available[atom])
            {
                GPasteSpecialAtomCallbackData *d = g_new (GPasteSpecialAtomCallbackData, 1);
                d->history = g_object_ref (priv->history);
                d->item = g_object_ref (item);
                d->atom = atom;
                gtk_clipboard_request_contents (g_paste_clipboard_get_real (clipboard), g_paste_special_atom_get (atom), special_contents_received, d);
            }
        }
    }

    g_paste_clipboards_manager_notify_finish (priv, clipboard, item, synchronized_text, something_in_clipboard);
}

static void
g_paste_clipboards_manager_image_ready (GPasteClipboard *clipboard,
                                        GdkPixbuf       *image,
                                        gpointer         user_data)
{
    g_autofree GPasteClipboardsManagerCallbackData *data = user_data;
    GPasteClipboardsManagerPrivate *priv = data->priv;
    GPasteItem *item = NULL;

    g_debug ("clipboards-manager: image ready");

    /* Did we already have some contents, or did we get some now? */
    gboolean something_in_clipboard = !!g_paste_clipboard_get_image_checksum (clipboard);

    /* If our contents got updated */
    if (image && data->track)
        item = G_PASTE_ITEM (g_paste_image_item_new (image));

    g_paste_clipboards_manager_notify_finish (priv, clipboard, item, NULL, something_in_clipboard);
}

/* TODO: move part of this to GPasteClipboard to drop all set_* */
static void
g_paste_clipboards_manager_targets_ready (GtkClipboard     *clipboard G_GNUC_UNUSED,
                                          GtkSelectionData *_targets,
                                          gpointer          user_data)
{
    g_autofree GPasteClipboardsManagerCallbackData *data = user_data;
    GPasteClipboardsManagerPrivate *priv = data->priv;

    g_debug ("clipboards-manager: targets ready");

    if (gtk_selection_data_get_length (_targets) >= 0)
    {
        g_autofree GdkAtom *targets = NULL;
        gint n_targets;

        gtk_selection_data_get_targets (_targets, &targets, &n_targets);
        data->uris_available = gtk_targets_include_uri (targets, n_targets);

        for (GPasteSpecialAtom atom = G_PASTE_SPECIAL_ATOM_FIRST; atom < G_PASTE_SPECIAL_ATOM_LAST; ++atom)
        {
            for (gint i = 0; i < n_targets; ++i)
            {
                if (targets[i] == g_paste_special_atom_get (atom))
                    data->special_atom_available[atom] = TRUE;
            }
        }

        if (data->uris_available || gtk_targets_include_text (targets, n_targets))
        {
            /* Update our cache from the real Clipboard */
            g_paste_clipboard_set_text (data->clip,
                                        g_paste_clipboards_manager_text_ready,
                                        data);
            data = NULL;
        }
        else if (g_paste_settings_get_images_support (priv->settings) && gtk_targets_include_image (targets, n_targets, FALSE))
        {
            /* Update our cache from the real Clipboard */
            g_paste_clipboard_set_image (data->clip,
                                         g_paste_clipboards_manager_image_ready,
                                         data);
            data = NULL;
        }
    }
    else
    {
        g_debug ("clipboards-manager: no target ready, trying text as fallback");

        data->fallback = TRUE;
        g_paste_clipboard_set_text (data->clip,
                                    g_paste_clipboards_manager_text_ready,
                                    data);
        data = NULL;
    }
}

static void
g_paste_clipboards_manager_notify (GPasteClipboard     *clipboard,
                                   GdkEventOwnerChange *event,
                                   gpointer             user_data)
{
    GPasteClipboardsManagerPrivate *priv = user_data;

    if (event->reason != GDK_OWNER_CHANGE_NEW_OWNER)
    {
        g_debug ("clipboards-manager: ignoring deletion event");
        return;
    }

    g_debug ("clipboards-manager: notify");

    GPasteSettings *settings = priv->settings;
    gboolean track = (g_paste_settings_get_track_changes (settings) &&
                          (g_paste_clipboard_is_clipboard (clipboard) ||             // We're not primary
                           g_paste_settings_get_primary_to_history (settings) ||     // Or we asked that primary affects clipboard
                           g_paste_settings_get_synchronize_clipboards (settings))); // Or primary and clipboards are synchronized hence primary will affect history through clipboard
    GPasteClipboardsManagerCallbackData *data = g_new0 (GPasteClipboardsManagerCallbackData, 1);

    data->priv = priv;
    data->clip = clipboard;
    data->track = track;

    gtk_clipboard_request_contents (g_paste_clipboard_get_real (clipboard),
                                    gdk_atom_intern_static_string ("TARGETS"),
                                    g_paste_clipboards_manager_targets_ready,
                                    data);
}

/**
 * g_paste_clipboards_manager_activate:
 * @self: a #GPasteClipboardsManager instance
 *
 * Activate the #GPasteClipboardsManager
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_activate (GPasteClipboardsManager *self)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARDS_MANAGER (self));

    GPasteClipboardsManagerPrivate *priv = g_paste_clipboards_manager_get_instance_private (self);

    for (GSList *clipboard = priv->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        _Clipboard *clip = clipboard->data;

        clip->c_signals[C_CLIP_OWNER_CHANGE] = g_signal_connect (clip->clipboard,
                                                                 "owner-change",
                                                                 G_CALLBACK (g_paste_clipboards_manager_notify),
                                                                 priv);
    }
}

/**
 * g_paste_clipboards_manager_select:
 * @self: a #GPasteClipboardsManager instance
 * @item: the #GPasteItem to select
 *
 * Select a new #GPasteItem
 *
 * Returns: %FALSE if the item was invalid, %TRUE otherwise
 */
G_PASTE_VISIBLE gboolean
g_paste_clipboards_manager_select (GPasteClipboardsManager *self,
                                   GPasteItem              *item)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIPBOARDS_MANAGER (self), FALSE);
    g_return_val_if_fail (_G_PASTE_IS_ITEM (item), FALSE);

    const GPasteClipboardsManagerPrivate *priv = _g_paste_clipboards_manager_get_instance_private (self);

    g_debug ("clipboards-manager: select");

    for (GSList *clipboard = priv->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        _Clipboard *clip = clipboard->data;

        if (!g_paste_clipboard_select_item (clip->clipboard, item))
        {
            g_debug ("clipboards-manager: item was invalid, deleting it");
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * g_paste_clipboards_manager_store:
 * @self: a #GPasteClipboardsManager instance
 *
 * Store clipboards contents before exiting
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_store (GPasteClipboardsManager *self)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARDS_MANAGER (self));

    const GPasteClipboardsManagerPrivate *priv = _g_paste_clipboards_manager_get_instance_private (self);

    g_debug ("clipboards-manager: store");

    for (GSList *clipboard = priv->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        _Clipboard *clip = clipboard->data;

        g_paste_clipboard_store (clip->clipboard);
    }
}

static void
on_item_selected (GPasteClipboardsManager *self,
                  GPasteItem              *item,
                  GPasteHistory           *history G_GNUC_UNUSED)
{
    GPasteClipboardsManagerPrivate *priv = g_paste_clipboards_manager_get_instance_private (G_PASTE_CLIPBOARDS_MANAGER (self));

    if (!g_paste_clipboards_manager_select (self, item))
        g_paste_history_remove (priv->history, 0);
}

static void
_clipboard_free (gpointer data)
{
    _Clipboard *clip = data;

    g_signal_handler_disconnect (clip->clipboard, clip->c_signals[C_CLIP_OWNER_CHANGE]);
    g_object_unref (clip->clipboard);
    g_free (clip);
}

static void
g_paste_clipboards_manager_dispose (GObject *object)
{
    GPasteClipboardsManagerPrivate *priv = g_paste_clipboards_manager_get_instance_private (G_PASTE_CLIPBOARDS_MANAGER (object));
    GPasteSettings *settings = priv->settings;

    if (settings)
    {
        g_signal_handler_disconnect (settings, priv->c_signals[C_SELECTED]);
        g_clear_object (&priv->settings);
        g_clear_object (&priv->history);
    }

    if (priv->clipboards)
    {
        g_slist_free_full (priv->clipboards, _clipboard_free);
        priv->clipboards = NULL;
    }

    G_OBJECT_CLASS (g_paste_clipboards_manager_parent_class)->dispose (object);
}

static void
g_paste_clipboards_manager_class_init (GPasteClipboardsManagerClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_clipboards_manager_dispose;
}

static void
g_paste_clipboards_manager_init (GPasteClipboardsManager *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_clipboards_manager_new:
 * @history: (transfer none): a #GPasteHistory instance
 * @settings: (transfer none): a #GPasteSettings instance
 *
 * Create a new instance of #GPasteClipboardsManager
 *
 * Returns: a newly allocated #GPasteClipboardsManager
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClipboardsManager *
g_paste_clipboards_manager_new (GPasteHistory  *history,
                                GPasteSettings *settings)
{
    g_return_val_if_fail (_G_PASTE_IS_HISTORY (history), NULL);
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);

    GPasteClipboardsManager *self = g_object_new (G_PASTE_TYPE_CLIPBOARDS_MANAGER, NULL);
    GPasteClipboardsManagerPrivate *priv = g_paste_clipboards_manager_get_instance_private (self);

    priv->history = g_object_ref (history);
    priv->settings = g_object_ref (settings);

    priv->c_signals[C_SELECTED] = g_signal_connect_swapped (history,
                                                            "selected",
                                                            G_CALLBACK (on_item_selected),
                                                            self);

    return self;
}
