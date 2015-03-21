/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gpaste-clipboards-manager-private.h"

#include <gpaste-image-item.h>
#include <gpaste-text-item.h>
#include <gpaste-uris-item.h>

struct _GPasteClipboardsManagerPrivate
{
    GSList         *clipboards;
    GPasteHistory  *history;
    GPasteSettings *settings;

    gulong          selected_signal;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteClipboardsManager, g_paste_clipboards_manager, G_TYPE_OBJECT)

/**
 * g_paste_clipboards_manager_add_clipboard:
 * @self: a #GPasteClipboardsManager instance
 * @clipboard: (transfer none): the GPasteClipboard to add
 *
 * Add a #GPasteClipboard to the #GPasteClipboardsManager
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_add_clipboard (GPasteClipboardsManager *self,
                                          GPasteClipboard         *clipboard)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));
    g_return_if_fail (G_PASTE_IS_CLIPBOARD (clipboard));

    GPasteClipboardsManagerPrivate *priv = g_paste_clipboards_manager_get_instance_private (self);

    priv->clipboards = g_slist_prepend (priv->clipboards, g_object_ref (clipboard));
    g_paste_clipboard_bootstrap (clipboard, priv->history);
}

static void
g_paste_clipboards_manager_sync_ready (GtkClipboard *clipboard G_GNUC_UNUSED,
                                       const gchar  *text,
                                       gpointer user_data)
{
    if (text)
        gtk_clipboard_set_text (user_data, text, -1);
}

/**
 * g_paste_clipboards_manager_sync_from_to:
 * @self: a #GPasteClipboardsManager instance
 * @from: the Source clipboard type
 * @to: the destination clipboard type
 *
 * Sync a clipboard into another
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_sync_from_to (GPasteClipboardsManager *self,
                                         GdkAtom                  from,
                                         GdkAtom                  to)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));

    GPasteClipboardsManagerPrivate *priv = g_paste_clipboards_manager_get_instance_private (self);
    GtkClipboard *_from = NULL, *_to = NULL;

    for (GSList *clipboard = priv->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        GPasteClipboard *clip = clipboard->data;
        GdkAtom cur = g_paste_clipboard_get_target (clip);

        if (cur == from)
            _from = g_paste_clipboard_get_real (clip);
        else if (cur == to)
            _to = g_paste_clipboard_get_real (clip);
    }

    if (_from && _to)
    {
        gtk_clipboard_request_text (_from,
                                    g_paste_clipboards_manager_sync_ready,
                                    _to);
    }
}

static void
g_paste_clipboards_manager_notify_finish (GPasteClipboardsManagerPrivate *priv,
                                          GPasteClipboard                *clipboard,
                                          GPasteItem                     *item,
                                          const gchar                    *synchronized_text,
                                          gboolean                        something_in_clipboard)
{
    GPasteHistory *history = priv->history;

    if (item)
        g_paste_history_add (history, item);

    if (!something_in_clipboard)
    {
        const GSList *hist = g_paste_history_get_history (history);
        if (hist)
            g_paste_clipboard_select_item (clipboard, hist->data);
    }

    if (synchronized_text)
    {
        for (GSList *_clipboard = priv->clipboards; _clipboard; _clipboard = g_slist_next (_clipboard))
        {
            GPasteClipboard *clip = _clipboard->data;

            if (clipboard == clip)
                continue;

            const gchar *text = g_paste_clipboard_get_text (clip);

            if (!text || g_strcmp0 (text, synchronized_text))
                g_paste_clipboard_select_text (clip, synchronized_text);
        }
    }
}

typedef struct {
    GPasteClipboardsManagerPrivate *priv;
    GPasteClipboard                *clip;
    gboolean                        track;
    gboolean                        uris_available;
} GPasteClipboardsManagerCallbackData;

static void
g_paste_clipboards_manager_text_ready (GPasteClipboard *clipboard,
                                       const gchar     *text,
                                       gpointer         user_data)
{
    G_PASTE_CLEANUP_FREE GPasteClipboardsManagerCallbackData *data = user_data;
    GPasteClipboardsManagerPrivate *priv = data->priv;
    GPasteItem *item = NULL;
    const gchar *synchronized_text = NULL;

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

    g_paste_clipboards_manager_notify_finish (priv, clipboard, item, synchronized_text, something_in_clipboard);
}

static void
g_paste_clipboards_manager_image_ready (GPasteClipboard *clipboard,
                                        GdkPixbuf       *image,
                                        gpointer         user_data)
{
    G_PASTE_CLEANUP_FREE GPasteClipboardsManagerCallbackData *data = user_data;
    GPasteClipboardsManagerPrivate *priv = data->priv;
    GPasteItem *item = NULL;

    /* Did we already have some contents, or did we get some now? */
    gboolean something_in_clipboard = !!g_paste_clipboard_get_image_checksum (clipboard);

    /* If our contents got updated */
    if (image && data->track)
        item = G_PASTE_ITEM (g_paste_image_item_new (image));

    g_paste_clipboards_manager_notify_finish (priv, clipboard, item, NULL, something_in_clipboard);
}

static void
g_paste_clipboards_manager_targets_ready (GtkClipboard     *clipboard G_GNUC_UNUSED,
                                          GtkSelectionData *targets,
                                          gpointer          user_data)
{
    G_PASTE_CLEANUP_FREE GPasteClipboardsManagerCallbackData *data = user_data;

    if (gtk_selection_data_get_length (targets) >= 0)
    {
        data->uris_available = gtk_selection_data_targets_include_uri (targets);

        if (data->uris_available || gtk_selection_data_targets_include_text (targets))
        {
            /* Update our cache from the real Clipboard */
            g_paste_clipboard_set_text (data->clip,
                                        g_paste_clipboards_manager_text_ready,
                                        data);
            data = NULL;
        }
        else if (g_paste_settings_get_images_support (data->priv->settings) && gtk_selection_data_targets_include_image (targets, FALSE))
        {
            /* Update our cache from the real Clipboard */
            g_paste_clipboard_set_image (data->clip,
                                         g_paste_clipboards_manager_image_ready,
                                         data);
            data = NULL;
        }
    }
}

static void
g_paste_clipboards_manager_notify (GPasteClipboard *clipboard,
                                   GdkEvent        *event G_GNUC_UNUSED,
                                   gpointer         user_data)
{
    GPasteClipboardsManagerPrivate *priv = user_data;

    GPasteSettings *settings = priv->settings;
    GdkAtom atom = g_paste_clipboard_get_target (clipboard);
    gboolean track = ((atom != GDK_SELECTION_PRIMARY || g_paste_settings_get_primary_to_history (settings)) &&
                      g_paste_settings_get_track_changes (settings));

    for (GSList *_clipboard = priv->clipboards; _clipboard; _clipboard = g_slist_next (_clipboard))
    {
        GPasteClipboard *clip = _clipboard->data;

        if (g_paste_clipboard_get_target (clip) != atom)
            continue;

        GPasteClipboardsManagerCallbackData *data = g_new (GPasteClipboardsManagerCallbackData, 1);

        data->priv = priv;
        data->clip = clip;
        data->track = track;

        gtk_clipboard_request_contents (g_paste_clipboard_get_real (clip),
                                        gdk_atom_intern_static_string ("TARGETS"),
                                        g_paste_clipboards_manager_targets_ready,
                                        data);
    }
}

/**
 * g_paste_clipboards_manager_activate:
 * @self: a #GPasteClipboardsManager instance
 *
 * Activate the #GPasteClipboardsManager
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_activate (GPasteClipboardsManager *self)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));

    GPasteClipboardsManagerPrivate *priv = g_paste_clipboards_manager_get_instance_private (self);

    for (GSList *clipboard = priv->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        g_signal_connect (G_PASTE_CLIPBOARD (clipboard->data),
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
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_select (GPasteClipboardsManager *self,
                                   const GPasteItem        *item)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));
    g_return_if_fail (G_PASTE_IS_ITEM (item));

    GPasteClipboardsManagerPrivate *priv = g_paste_clipboards_manager_get_instance_private (self);

    for (GSList *clipboard = priv->clipboards; clipboard; clipboard = g_slist_next (clipboard))
        g_paste_clipboard_select_item (clipboard->data, item);
}

static void
on_item_selected (GPasteClipboardsManager *self,
                  GPasteItem              *item,
                  GPasteHistory           *history G_GNUC_UNUSED)
{
    g_paste_clipboards_manager_select (self, item);
}

static void
g_paste_clipboards_manager_dispose (GObject *object)
{
    GPasteClipboardsManagerPrivate *priv = g_paste_clipboards_manager_get_instance_private (G_PASTE_CLIPBOARDS_MANAGER (object));
    GPasteSettings *settings = priv->settings;

    if (settings)
    {
        g_signal_handler_disconnect (settings, priv->selected_signal);
        g_clear_object (&priv->settings);
        g_clear_object (&priv->history);
    }

    G_OBJECT_CLASS (g_paste_clipboards_manager_parent_class)->dispose (object);
}

static void
g_paste_clipboards_manager_finalize (GObject *object)
{
    GPasteClipboardsManagerPrivate *priv = g_paste_clipboards_manager_get_instance_private (G_PASTE_CLIPBOARDS_MANAGER (object));

    g_slist_free_full (priv->clipboards,
                       g_object_unref);

    G_OBJECT_CLASS (g_paste_clipboards_manager_parent_class)->finalize (object);
}

static void
g_paste_clipboards_manager_class_init (GPasteClipboardsManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_clipboards_manager_dispose;
    object_class->finalize = g_paste_clipboards_manager_finalize;
}

static void
g_paste_clipboards_manager_init (GPasteClipboardsManager *self)
{
    GPasteClipboardsManagerPrivate *priv = g_paste_clipboards_manager_get_instance_private (self);

    priv->clipboards = NULL;
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
    g_return_val_if_fail (G_PASTE_IS_HISTORY (history), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    GPasteClipboardsManager *self = g_object_new (G_PASTE_TYPE_CLIPBOARDS_MANAGER, NULL);
    GPasteClipboardsManagerPrivate *priv = g_paste_clipboards_manager_get_instance_private (self);

    priv->history = g_object_ref (history);
    priv->settings = g_object_ref (settings);

    priv->selected_signal = g_signal_connect_swapped (history,
                                                      "selected",
                                                      G_CALLBACK (on_item_selected),
                                                      self);

    return self;
}
