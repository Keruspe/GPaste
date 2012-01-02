/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#define G_PASTE_CLIPBOARDS_MANAGER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_CLIPBOARDS_MANAGER, GPasteClipboardsManagerPrivate))

G_DEFINE_TYPE (GPasteClipboardsManager, g_paste_clipboards_manager, G_TYPE_OBJECT)

struct _GPasteClipboardsManagerPrivate
{
    GSList *clipboards;
    GPasteHistory *history;
    GPasteSettings *settings;
};

/**
 * g_paste_clipboards_manager_add_clipboard:
 * @self: a GPasteClipboardsManager instance
 * @clipboard: (transfer none): the GPasteClipboard to add
 *
 * Add a GPasteClipboard to the GPasteClipboardsManager
 *
 * Returns:
 */
void
g_paste_clipboards_manager_add_clipboard (GPasteClipboardsManager *self,
                                          GPasteClipboard         *clipboard)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));
    g_return_if_fail (G_PASTE_IS_CLIPBOARD (clipboard));

    GPasteClipboardsManagerPrivate *priv = self->priv;
    GtkClipboard *real = g_paste_clipboard_get_real (clipboard);

    priv->clipboards = g_slist_prepend (priv->clipboards, clipboard);

    if (gtk_clipboard_wait_is_uris_available (real) ||
        gtk_clipboard_wait_is_text_available (real))
            g_paste_clipboard_set_text (clipboard);
    else if (gtk_clipboard_wait_is_image_available (real))
        g_paste_clipboard_set_image (clipboard);

    if (g_paste_clipboard_get_text (clipboard) == NULL &&
        g_paste_clipboard_get_image_checksum (clipboard) == NULL)
    {
        const GSList *history = g_paste_history_get_history (priv->history);
        if (history != NULL)
            g_paste_clipboard_select_item (clipboard, history->data);
    }
}

static gboolean
g_paste_clipboards_manager_check_clipboards (gpointer user_data)
{
    GPasteClipboardsManager *self = G_PASTE_CLIPBOARDS_MANAGER (user_data);
    GPasteClipboardsManagerPrivate *priv = self->priv;
    GPasteHistory *history = priv->history;
    GPasteSettings *settings = priv->settings;
    const gchar *synchronized_text = NULL;

    for (GSList *clipboard = priv->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        GPasteClipboard *clip = clipboard->data;
        GtkClipboard *real = g_paste_clipboard_get_real (clip);
        gboolean something_in_clipboard = FALSE;
        gboolean uris_available = gtk_clipboard_wait_is_uris_available (real);

        if (uris_available || gtk_clipboard_wait_is_text_available (real))
        {
            const gchar *text = g_paste_clipboard_set_text (clip);

            something_in_clipboard = (g_paste_clipboard_get_text (clip) != NULL);

            if (text != NULL)
            {
                if (g_paste_settings_get_track_changes (settings) &&
                    (g_paste_clipboard_get_target (clip) == GDK_SELECTION_CLIPBOARD ||
                        g_paste_settings_get_primary_to_history (settings)))
                {
                    GPasteItem *item;

                    if (uris_available)
                        item = G_PASTE_ITEM (g_paste_uris_item_new (text));
                    else
                        item = G_PASTE_ITEM (g_paste_text_item_new (text));

                    g_paste_history_add (history, item);
                    g_object_unref (item);
                }

                if (g_paste_settings_get_synchronize_clipboards (settings))
                    synchronized_text = text;
            }
        }
        else if (gtk_clipboard_wait_is_image_available (real))
        {
            GdkPixbuf *image = g_paste_clipboard_set_image (clip);

            something_in_clipboard = (g_paste_clipboard_get_image_checksum (clip) != NULL);

            if (image != NULL)
            {
                if (g_paste_settings_get_track_changes (settings) &&
                    (g_paste_clipboard_get_target (clip) == GDK_SELECTION_CLIPBOARD ||
                        g_paste_settings_get_primary_to_history (settings)))
                {
                    GPasteItem *item = G_PASTE_ITEM (g_paste_image_item_new (image));

                    g_paste_history_add (history, item);
                    gdk_pixbuf_unref (image);
                    g_object_unref (item);
                }
            }
        }

        if (!something_in_clipboard)
        {
            const GSList *hist = g_paste_history_get_history (history);
            if (hist != NULL)
                g_paste_clipboard_select_item (clip, hist->data);
        }
    }

    if (synchronized_text != NULL)
    {
        for (GSList *clipboard = priv->clipboards; clipboard; clipboard = g_slist_next (clipboard))
        {
            GPasteClipboard *clip = clipboard->data;
            const gchar *text = g_paste_clipboard_get_text (clip);

            if (text == NULL ||
                g_strcmp0 (text, synchronized_text) != 0)
                    g_paste_clipboard_select_text (clip, synchronized_text);
        }
    }

    return TRUE;
}

/**
 * g_paste_clipboards_manager_activate:
 * @self: a GPasteClipboardsManager instance
 *
 * Activate the GPasteClipboardsManager
 *
 * Returns:
 */
void
g_paste_clipboards_manager_activate (GPasteClipboardsManager *self)
{
    g_timeout_add_seconds (1, g_paste_clipboards_manager_check_clipboards, self);
}

/**
 * g_paste_clipboards_manager_select:
 * @self: a GPasteClipboardsManager instance
 * @item: the GPasteItem to select
 *
 * Select a new GPasteItem
 *
 * Returns:
 */
void
g_paste_clipboards_manager_select (GPasteClipboardsManager *self,
                                   GPasteItem              *item)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));
    g_return_if_fail (G_PASTE_IS_ITEM (item));

    GPasteClipboardsManagerPrivate *priv = self->priv;

    g_paste_history_add (priv->history, item);
    for (GSList *clipboard = priv->clipboards; clipboard; clipboard = g_slist_next (clipboard))
        g_paste_clipboard_select_item (clipboard->data, item);
}

static void
g_paste_clipboards_manager_dispose (GObject *object)
{
    GPasteClipboardsManagerPrivate *priv = G_PASTE_CLIPBOARDS_MANAGER (object)->priv;

    g_object_unref (priv->settings);
    g_object_unref (priv->history);

    G_OBJECT_CLASS (g_paste_clipboards_manager_parent_class)->dispose (object);
}

static void
g_paste_clipboards_manager_finalize (GObject *object)
{
    GPasteClipboardsManagerPrivate *priv = G_PASTE_CLIPBOARDS_MANAGER (object)->priv;

    g_slist_free_full (priv->clipboards,
                       g_object_unref);

    G_OBJECT_CLASS (g_paste_clipboards_manager_parent_class)->finalize (object);
}

static void
g_paste_clipboards_manager_class_init (GPasteClipboardsManagerClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPasteClipboardsManagerPrivate));

    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_clipboards_manager_dispose;
    object_class->finalize = g_paste_clipboards_manager_finalize;
}

static void
g_paste_clipboards_manager_init (GPasteClipboardsManager *self)
{
    self->priv = G_PASTE_CLIPBOARDS_MANAGER_GET_PRIVATE (self);
}

static gboolean
on_item_selected (GPasteClipboardsManager *self,
                  GPasteItem              *item,
                  gpointer                 user_data)
{
    /* silence warning */
    user_data = user_data;

    g_paste_clipboards_manager_select (self, item);

    return TRUE;
}

/**
 * g_paste_clipboards_manager_new:
 * @history: (transfer none): a GPasteHistory instance
 * @settings: (transfer none): a GPasteSettings instance
 *
 * Create a new instance of GPasteClipboardsManager
 *
 * Returns: a newly allocated GPasteClipboardsManager
 *          free it with g_object_unref
 */
GPasteClipboardsManager *
g_paste_clipboards_manager_new (GPasteHistory  *history,
                                GPasteSettings *settings)
{
    GPasteClipboardsManager *self = g_object_new (G_PASTE_TYPE_CLIPBOARDS_MANAGER, NULL);
    GPasteClipboardsManagerPrivate *priv = self->priv;

    priv->history = g_object_ref (history);
    priv->settings = g_object_ref (settings);
    priv->clipboards = NULL;

    g_signal_connect_swapped (G_OBJECT (history),
                              "selected",
                              G_CALLBACK (on_item_selected),
                              self);

    return self;
}
