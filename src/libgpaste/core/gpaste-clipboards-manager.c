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
    C_CLIP_CHANGED,

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
g_paste_clipboards_manager_clipboard_updated (GPasteClipboard *clipboard,
                                              GPasteItem      *item,
                                              const gchar     *synchronized_text,
                                              gboolean         something_in_clipboard,
                                              gpointer         user_data)
{
    GPasteClipboardsManagerPrivate *priv = user_data;
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

static void
g_paste_clipboards_manager_notify (GPasteClipboard *clipboard,
                                   gpointer         user_data)
{
    GPasteClipboardsManagerPrivate *priv = user_data;
    GdkClipboard *real = g_paste_clipboard_get_real (clipboard);

    // If we just took ownership of the clipboard, ignore the event we induced
    if (gdk_clipboard_is_local (real))
        return;

    g_debug ("clipboards-manager: notify");

    g_paste_clipboard_update (clipboard, priv->history, g_paste_clipboards_manager_clipboard_updated, priv);
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

        clip->c_signals[C_CLIP_CHANGED] = g_signal_connect (clip->clipboard,
                                                            "changed",
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
            return FALSE;
    }

    return TRUE;
}

static void
on_item_selected (GPasteClipboardsManager *self,
                  GPasteItem              *item,
                  GPasteHistory           *history G_GNUC_UNUSED)
{
    GPasteClipboardsManagerPrivate *priv = g_paste_clipboards_manager_get_instance_private (G_PASTE_CLIPBOARDS_MANAGER (self));

    if (!g_paste_clipboards_manager_select (self, item))
    {
        g_debug ("clipboards-manager: item was invalid, deleting it");
        g_paste_history_remove (priv->history, 0);
    }
}

static void
_clipboard_free (gpointer data)
{
    _Clipboard *clip = data;

    g_signal_handler_disconnect (clip->clipboard, clip->c_signals[C_CLIP_CHANGED]);
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
