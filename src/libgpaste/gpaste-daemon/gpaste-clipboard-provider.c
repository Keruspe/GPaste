// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-clipboard-provider.h>

G_DEFINE_INTERFACE (GPasteClipboardProvider, g_paste_clipboard_provider, G_TYPE_OBJECT)

enum
{
    CHANGED,

    LAST_SIGNAL
};

static guint64 signals[LAST_SIGNAL] = { 0 };

static void
g_paste_clipboard_provider_default_init (GPasteClipboardProviderInterface *iface G_GNUC_UNUSED)
{
    /**
     * GPasteClipboardProvider::changed:
     * @provider: the object on which the signal was emitted
     *
     * The "changed" signal is emitted when GPaste receives an event that
     * indicates that the ownership of the underlying selection has changed
     * externally (i.e. not as the result of one of our own writes).
     */
    signals[CHANGED] = g_signal_new ("changed",
                                     G_PASTE_TYPE_CLIPBOARD_PROVIDER,
                                     G_SIGNAL_RUN_FIRST,
                                     0,    /* class offset     */
                                     NULL, /* accumulator      */
                                     NULL, /* accumulator data */
                                     g_cclosure_marshal_VOID__VOID,
                                     G_TYPE_NONE,
                                     0);
}

/**
 * g_paste_clipboard_provider_is_clipboard:
 * @self: a #GPasteClipboardProvider instance
 *
 * Get whether this provider drives the clipboard or the primary selection
 *
 * Returns: %TRUE if this provider drives the clipboard
 */
G_PASTE_VISIBLE gboolean
g_paste_clipboard_provider_is_clipboard (const GPasteClipboardProvider *self)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIPBOARD_PROVIDER (self), FALSE);

    return G_PASTE_CLIPBOARD_PROVIDER_GET_IFACE ((GPasteClipboardProvider *) self)->is_clipboard (self);
}

/**
 * g_paste_clipboard_provider_get_text:
 * @self: a #GPasteClipboardProvider instance
 *
 * Get the text currently cached by the provider
 *
 * Returns: read-only string containing the text or %NULL
 */
G_PASTE_VISIBLE const gchar *
g_paste_clipboard_provider_get_text (const GPasteClipboardProvider *self)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIPBOARD_PROVIDER (self), NULL);

    return G_PASTE_CLIPBOARD_PROVIDER_GET_IFACE ((GPasteClipboardProvider *) self)->get_text (self);
}

/**
 * g_paste_clipboard_provider_get_image_checksum:
 * @self: a #GPasteClipboardProvider instance
 *
 * Get the checksum of the image currently cached by the provider
 *
 * Returns: read-only string containing the checksum or %NULL
 */
G_PASTE_VISIBLE const gchar *
g_paste_clipboard_provider_get_image_checksum (const GPasteClipboardProvider *self)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIPBOARD_PROVIDER (self), NULL);

    return G_PASTE_CLIPBOARD_PROVIDER_GET_IFACE ((GPasteClipboardProvider *) self)->get_image_checksum (self);
}

/**
 * g_paste_clipboard_provider_is_empty:
 * @self: a #GPasteClipboardProvider instance
 *
 * Get whether the provider currently holds no content
 *
 * Returns: %TRUE if the provider holds nothing
 */
G_PASTE_VISIBLE gboolean
g_paste_clipboard_provider_is_empty (const GPasteClipboardProvider *self)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIPBOARD_PROVIDER (self), TRUE);

    return G_PASTE_CLIPBOARD_PROVIDER_GET_IFACE ((GPasteClipboardProvider *) self)->is_empty (self);
}

/**
 * g_paste_clipboard_provider_update:
 * @self: a #GPasteClipboardProvider instance
 * @callback: (scope async): the callback to be called when the content is ready
 * @user_data: user data to pass to @callback
 *
 * Read the current selection content and update the internal cache. The
 * callback receives a newly created #GPasteItem or %NULL if the content is
 * unchanged, unrecognised, or the selection has no owner.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_provider_update (GPasteClipboardProvider              *self,
                                   GPasteClipboardProviderUpdateCallback callback,
                                   gpointer                              user_data)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD_PROVIDER (self));

    G_PASTE_CLIPBOARD_PROVIDER_GET_IFACE (self)->update (self, callback, user_data);
}

/**
 * g_paste_clipboard_provider_select_text:
 * @self: a #GPasteClipboardProvider instance
 * @text: the text to select
 *
 * Put the text into the provider and the underlying selection
 */
G_PASTE_VISIBLE void
g_paste_clipboard_provider_select_text (GPasteClipboardProvider *self,
                                        const gchar             *text)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD_PROVIDER (self));
    g_return_if_fail (text);
    g_return_if_fail (g_utf8_validate (text, -1, NULL));

    G_PASTE_CLIPBOARD_PROVIDER_GET_IFACE (self)->select_text (self, text);
}

/**
 * g_paste_clipboard_provider_sync_text:
 * @self: the source #GPasteClipboardProvider instance
 * @other: the target #GPasteClipboardProvider instance
 *
 * Synchronise the text between two providers
 */
G_PASTE_VISIBLE void
g_paste_clipboard_provider_sync_text (const GPasteClipboardProvider *self,
                                      GPasteClipboardProvider       *other)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD_PROVIDER (self));
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD_PROVIDER (other));

    G_PASTE_CLIPBOARD_PROVIDER_GET_IFACE ((GPasteClipboardProvider *) self)->sync_text (self, other);
}

/**
 * g_paste_clipboard_provider_select_item:
 * @self: a #GPasteClipboardProvider instance
 * @item: the item to select
 *
 * Put the value of the item into the provider and the underlying selection
 *
 * Returns: %FALSE if the item was invalid, %TRUE otherwise
 */
G_PASTE_VISIBLE gboolean
g_paste_clipboard_provider_select_item (GPasteClipboardProvider *self,
                                        GPasteItem              *item)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIPBOARD_PROVIDER (self), FALSE);
    g_return_val_if_fail (_G_PASTE_IS_ITEM (item), FALSE);

    return G_PASTE_CLIPBOARD_PROVIDER_GET_IFACE (self)->select_item (self, item);
}

/**
 * g_paste_clipboard_provider_ensure_not_empty:
 * @self: a #GPasteClipboardProvider instance
 * @history: a #GPasteHistory instance
 *
 * Ensure the selection has some contents (as long as the history's not empty)
 */
G_PASTE_VISIBLE void
g_paste_clipboard_provider_ensure_not_empty (GPasteClipboardProvider *self,
                                             GPasteHistory           *history)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD_PROVIDER (self));
    g_return_if_fail (_G_PASTE_IS_HISTORY (history));

    /* Identical for every backend: if we hold nothing, re-own the selection with
     * the history's head (dropping it if the backend rejects it). Backends only
     * report emptiness through the is_empty vfunc. */
    if (!g_paste_clipboard_provider_is_empty (self))
        return;

    const GList *hist = g_paste_history_get_history (history);

    if (!hist)
        return;

    GPasteItem *item = hist->data;

    if (!g_paste_clipboard_provider_select_item (self, item))
        g_paste_history_remove (history, 0);
}

/**
 * g_paste_clipboard_provider_store:
 * @self: a #GPasteClipboardProvider instance
 *
 * Store the contents of the selection before exiting
 */
G_PASTE_VISIBLE void
g_paste_clipboard_provider_store (GPasteClipboardProvider *self)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD_PROVIDER (self));

    G_PASTE_CLIPBOARD_PROVIDER_GET_IFACE (self)->store (self);
}

/**
 * g_paste_clipboard_provider_emit_changed:
 * @self: a #GPasteClipboardProvider instance
 *
 * Emit the #GPasteClipboardProvider::changed signal. Meant to be called by
 * implementations when their backend reports an external ownership change.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_provider_emit_changed (GPasteClipboardProvider *self)
{
    g_return_if_fail (_G_PASTE_IS_CLIPBOARD_PROVIDER (self));

    g_signal_emit (self, signals[CHANGED], 0);
}

/**
 * g_paste_clipboard_provider_target_name:
 * @is_clipboard: whether the provider drives the clipboard
 *
 * Returns: the selection's name ("CLIPBOARD" or "PRIMARY"), for debug logging
 */
G_PASTE_VISIBLE const gchar *
g_paste_clipboard_provider_target_name (gboolean is_clipboard)
{
    return is_clipboard ? "CLIPBOARD" : "PRIMARY";
}
