/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-clipboards-manager.h>

struct _GPasteClipboardsManager
{
    GObject parent_instance;
};

typedef struct
{
    GPasteClipboard *clipboard;
    GSignalGroup    *signal_group;
} _Clipboard;

typedef struct
{
    GSList         *clipboards;
    GPasteHistory  *history;
    GSignalGroup   *history_signals;
    GPasteSettings *settings;
} GPasteClipboardsManagerPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (ClipboardsManager, clipboards_manager, G_TYPE_OBJECT)

static void g_paste_clipboards_manager_notify (GPasteClipboard *clipboard, gpointer user_data);

static void
g_paste_clipboards_manager_bootstrap_ready (GPasteClipboard *clipboard,
                                            GPasteItem      *item G_GNUC_UNUSED,
                                            gpointer         user_data)
{
    GPasteClipboardsManagerPrivate *priv = user_data;
    g_paste_clipboard_ensure_not_empty (clipboard, priv->history);
}

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
    clip->signal_group = g_signal_group_new (G_PASTE_TYPE_CLIPBOARD);
    g_signal_group_connect (clip->signal_group, "changed", G_CALLBACK (g_paste_clipboards_manager_notify), priv);

    priv->clipboards = g_slist_prepend (priv->clipboards, clip);
    g_paste_clipboard_update (clipboard, g_paste_clipboards_manager_bootstrap_ready, priv);
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
    gboolean                        track;
} GPasteClipboardsManagerUpdateData;

static void
g_paste_clipboards_manager_update_ready (GPasteClipboard *clipboard,
                                         GPasteItem      *item,
                                         gpointer         user_data)
{
    g_autofree GPasteClipboardsManagerUpdateData *data = user_data;
    GPasteClipboardsManagerPrivate *priv = data->priv;

    g_debug ("clipboards-manager: update ready");

    const gchar *synchronized_text = NULL;

    if (item && g_paste_clipboard_get_text (clipboard) &&
        g_paste_settings_get_synchronize_clipboards (priv->settings))
        synchronized_text = g_paste_clipboard_get_text (clipboard);

    if (!data->track && item)
        g_clear_object (&item);

    gboolean something_in_clipboard = !!g_paste_clipboard_get_text (clipboard) ||
                                      !!g_paste_clipboard_get_image_checksum (clipboard);

    g_paste_clipboards_manager_notify_finish (priv, clipboard, item, synchronized_text, something_in_clipboard);
}

static void
g_paste_clipboards_manager_notify (GPasteClipboard *clipboard,
                                   gpointer         user_data)
{
    GPasteClipboardsManagerPrivate *priv = user_data;

    g_debug ("clipboards-manager: notify");

    GPasteSettings *settings = priv->settings;
    gboolean track = (g_paste_settings_get_track_changes (settings) &&
                          (g_paste_clipboard_is_clipboard (clipboard) ||             // We're not primary
                           g_paste_settings_get_primary_to_history (settings) ||     // Or we asked that primary affects clipboard
                           g_paste_settings_get_synchronize_clipboards (settings))); // Or primary and clipboards are synchronized hence primary will affect history through clipboard
    GPasteClipboardsManagerUpdateData *data = g_new0 (GPasteClipboardsManagerUpdateData, 1);

    data->priv = priv;
    data->track = track;

    g_paste_clipboard_update (clipboard,
                              g_paste_clipboards_manager_update_ready,
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

        g_signal_group_set_target (clip->signal_group, clip->clipboard);
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

    g_clear_object (&clip->signal_group);
    g_object_unref (clip->clipboard);
    g_free (clip);
}

static void
g_paste_clipboards_manager_dispose (GObject *object)
{
    GPasteClipboardsManagerPrivate *priv = g_paste_clipboards_manager_get_instance_private (G_PASTE_CLIPBOARDS_MANAGER (object));

    g_clear_object (&priv->history_signals);
    g_clear_object (&priv->history);
    g_clear_object (&priv->settings);

    g_clear_slist (&priv->clipboards, _clipboard_free);

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

    GSignalGroup *history_signals = priv->history_signals = g_signal_group_new (G_PASTE_TYPE_HISTORY);
    g_signal_group_connect_swapped (history_signals, "selected", G_CALLBACK (on_item_selected), self);
    g_signal_group_set_target (history_signals, history);

    return self;
}
