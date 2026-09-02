// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-clipboard-provider-private.h>
#include <gpaste-daemon/gpaste-password-item.h>

G_DEFINE_INTERFACE (GPasteClipboardProvider, g_paste_clipboard_provider, G_TYPE_OBJECT)

enum
{
    CHANGED,
    PUBLISHED,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

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

    /**
     * GPasteClipboardProvider::published:
     * @provider: the object on which the signal was emitted
     * @independent: whether this write starts a new copy
     *
     * A successful local write identifies the selection without another read.
     * Independent writes retire the previous copy's queued work. Maintenance
     * writes preserve copy order and any refresh queued for that same copy.
     */
    signals[PUBLISHED] = g_signal_new ("published",
                                       G_PASTE_TYPE_CLIPBOARD_PROVIDER,
                                       G_SIGNAL_RUN_FIRST,
                                       0, NULL, NULL,
                                       g_cclosure_marshal_VOID__BOOLEAN,
                                       G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
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
g_paste_clipboard_provider_is_clipboard (GPasteClipboardProvider *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD_PROVIDER (self), FALSE);

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
g_paste_clipboard_provider_get_text (GPasteClipboardProvider *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD_PROVIDER (self), NULL);

    return g_paste_clipboard_provider_is_reading (self) ? NULL : G_PASTE_CLIPBOARD_PROVIDER_GET_IFACE (self)->get_text (self);
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
g_paste_clipboard_provider_get_image_checksum (GPasteClipboardProvider *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD_PROVIDER (self), NULL);

    return g_paste_clipboard_provider_is_reading (self) ? NULL : G_PASTE_CLIPBOARD_PROVIDER_GET_IFACE (self)->get_image_checksum (self);
}

/**
 * g_paste_clipboard_provider_is_reading:
 * @self: a #GPasteClipboardProvider instance
 *
 * Returns: whether the current selection is still being classified; cached
 *          getters returning %NULL during this interval do not prove a change
 */
G_PASTE_VISIBLE gboolean
g_paste_clipboard_provider_is_reading (GPasteClipboardProvider *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD_PROVIDER (self), FALSE);

    return G_PASTE_CLIPBOARD_PROVIDER_GET_IFACE (self)->is_reading (self);
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
g_paste_clipboard_provider_is_empty (GPasteClipboardProvider *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD_PROVIDER (self), TRUE);

    return !g_paste_clipboard_provider_is_reading (self) && G_PASTE_CLIPBOARD_PROVIDER_GET_IFACE (self)->is_empty (self);
}

/**
 * g_paste_clipboard_provider_update:
 * @self: a #GPasteClipboardProvider instance
 * @callback: (scope async): the callback to be called when the content is ready
 * @user_data: the data to pass to @callback
 *
 * Read the current selection content and update the internal cache. The
 * callback receives a newly created #GPasteItem or %NULL if the content is
 * unchanged, unrecognised, or the selection has no owner. A superseded read
 * answers %NULL with @superseded set, and its callback only releases its state.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_provider_update (GPasteClipboardProvider              *self,
                                   GPasteClipboardProviderUpdateCallback callback,
                                   gpointer                              user_data)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARD_PROVIDER (self));

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
    g_paste_clipboard_provider_select_text_full (self, text, TRUE);
}

void
g_paste_clipboard_provider_select_text_full (GPasteClipboardProvider *self,
                                             const gchar             *text,
                                             gboolean                 independent)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARD_PROVIDER (self));
    g_return_if_fail (text);
    g_return_if_fail (g_utf8_validate (text, -1, NULL));

    G_PASTE_CLIPBOARD_PROVIDER_GET_IFACE (self)->select_text (self, text);
    g_signal_emit (self, signals[PUBLISHED], 0, independent);
}

/**
 * g_paste_clipboard_provider_sync_text:
 * @self: the source #GPasteClipboardProvider instance
 * @other: the target #GPasteClipboardProvider instance
 * @callback: what to put the text on @other with, once the read lands
 * @user_data: (nullable): the data to pass to @callback
 * @destroy: (nullable): how to release @user_data, run whenever the read is
 *           done with, @callback or no @callback
 *
 * Synchronise the text between two providers
 *
 * Reading is the provider's and publishing is the caller's: see
 * #GPasteClipboardSyncCallback for why the two are not one call.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_provider_sync_text (GPasteClipboardProvider    *self,
                                      GPasteClipboardProvider    *other,
                                      GPasteClipboardSyncCallback callback,
                                      gpointer                    user_data,
                                      GDestroyNotify              destroy)
{
    /* Hand-rolled where every other call here uses g_return_if_fail (): @destroy
     * is ours from this call on -- which is why both backends run it on their own
     * no-data returns -- so a bare return would leak @user_data and everything it
     * holds up, the manager and its selections included. Checked once, and the
     * release happens whether or not the warning compiles away. */
    if (!G_PASTE_IS_CLIPBOARD_PROVIDER (self) ||
        !G_PASTE_IS_CLIPBOARD_PROVIDER (other) ||
        !callback)
    {
        g_critical ("%s: assertion failed: valid @self, @other and @callback", G_STRFUNC);

        if (destroy)
            destroy (user_data);

        return;
    }

    G_PASTE_CLIPBOARD_PROVIDER_GET_IFACE ((GPasteClipboardProvider *) self)->sync_text (self, other, callback, user_data, destroy);
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
    return g_paste_clipboard_provider_select_item_full (self, item, TRUE);
}

gboolean
g_paste_clipboard_provider_select_item_full (GPasteClipboardProvider *self,
                                             GPasteItem              *item,
                                             gboolean                 independent)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD_PROVIDER (self), FALSE);
    g_return_val_if_fail (G_PASTE_IS_ITEM (item), FALSE);

    if (!G_PASTE_CLIPBOARD_PROVIDER_GET_IFACE (self)->select_item (self, item))
        return FALSE;

    g_signal_emit (self, signals[PUBLISHED], 0, independent);
    return TRUE;
}

/**
 * g_paste_clipboard_provider_ensure_not_empty:
 * @self: a #GPasteClipboardProvider instance
 * @history: a #GPasteHistory instance
 *
 * Ensure the selection has some contents (as long as the history's not empty
 * and its head is not a password)
 *
 * What comes back is what this put there, so a caller can act on a selection it
 * did not perform itself -- arming a password's countdown, above all, which
 * otherwise only the ordinary selection path does.
 *
 * Returns: (transfer none) (nullable): the #GPasteItem this put on the selection,
 *          or %NULL when it left the selection as it found it
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_clipboard_provider_ensure_not_empty (GPasteClipboardProvider *self,
                                             GPasteHistory           *history)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD_PROVIDER (self), NULL);
    g_return_val_if_fail (G_PASTE_IS_HISTORY (history), NULL);

    /* Identical for every backend: if we hold nothing, re-own the selection with
     * the history's head (dropping it if the backend rejects it). Backends only
     * report emptiness through the is_empty vfunc. */
    if (!g_paste_clipboard_provider_is_empty (self))
        return NULL;

    const GPtrArray *hist = g_paste_history_get_history (history);

    if (!hist->len)
        return NULL;

    GPasteItem *item = g_ptr_array_index (hist, 0);

    /* A selection falling empty under a password is, as often as not, its owner
     * taking that password back off -- a password manager clearing what it
     * copied, which the history recorded as a password item at its head. Putting
     * it back is exposure nobody asked for, and under a password-timeout of 0 it
     * would then stay there for the rest of the session. An emptied selection is
     * left empty instead: there is no other item the user chose to put there.
     *
     * A text head goes back, released or not, and that is not to be narrowed:
     * putting it back is the whole of why closing the application you copied
     * from does not lose the copy, on both backends. A password its manager
     * copied *without* the hint was recorded as plain text, and a release then
     * restores it like any text -- but nothing tells that text from any other,
     * and refusing to restore text on a release would take the feature away
     * from every copy to spare the one case the owner did not mark. The same
     * goes for PRIMARY, whose owners release it on a deselection. And a password
     * manager clearing its copy by writing the empty string, as they do, does
     * not come here at all: that string is content (see the DROP case in
     * g_paste_clipboard_update_conclude ()), and nothing is put back over it. */
    if (G_PASTE_IS_PASSWORD_ITEM (item))
        return NULL;

    if (g_paste_clipboard_provider_select_item_full (self, item, FALSE))
        return item;

    g_paste_history_remove (history, 0);

    return NULL;
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
    g_return_if_fail (G_PASTE_IS_CLIPBOARD_PROVIDER (self));

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
    g_return_if_fail (G_PASTE_IS_CLIPBOARD_PROVIDER (self));

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
