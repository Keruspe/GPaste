// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-history.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_CLIPBOARD_PROVIDER (g_paste_clipboard_provider_get_type ())

G_PASTE_VISIBLE
G_DECLARE_INTERFACE (GPasteClipboardProvider, g_paste_clipboard_provider, G_PASTE, CLIPBOARD_PROVIDER, GObject)

/**
 * GPasteClipboardSecret:
 * @CLIPBOARD_SECRET_NO: the selection was asked and says its content is not one
 * @CLIPBOARD_SECRET_YES: a hint came back and matched
 * @CLIPBOARD_SECRET_UNKNOWN: nothing could be learned either way
 *
 * What an update found out about the selection carrying a secret.
 *
 * %CLIPBOARD_SECRET_UNKNOWN is not a third kind of answer but the absence of
 * one: the selection could not be asked what it offers, or a hint it does offer
 * never came back. Only %CLIPBOARD_SECRET_YES ever makes a password -- proof is
 * what a secret takes, here as everywhere -- so classifying content reads the
 * unknown as a no. The one place the two part company is a record that outlives
 * the read that made it: retiring one on "the answer never came" is retiring it
 * on no evidence, and the secret is still sitting there.
 *
 * Plain C, with no GType of its own: it travels between a provider and the
 * clipboards manager and no further -- libgpaste-daemon is internal, and nothing
 * on the wire or in a binding ever names it. The enumerations a binding does see
 * are listed in the top-level AGENTS.md.
 */
typedef enum
{
    CLIPBOARD_SECRET_NO,
    CLIPBOARD_SECRET_YES,
    CLIPBOARD_SECRET_UNKNOWN,
} GPasteClipboardSecret;

/**
 * GPasteClipboardProviderUpdateCallback:
 * @self: (nullable): the #GPasteClipboardProvider whose content was read, which
 *        can only be %NULL for a @superseded read whose provider is gone
 * @item: (transfer full) (nullable): the newly created #GPasteItem, or %NULL
 *        when the content is unchanged, unrecognised or the selection is empty
 * @superseded: whether another selection change overtook this read; @item is
 *              %NULL and the callback must only release its state in that case
 * @secret: what the read found out about the owner marking its content a secret
 * @user_data: the data passed to g_paste_clipboard_provider_update()
 *
 * Receives the outcome of a g_paste_clipboard_provider_update(). The callback
 * **owns** @item: it must hand it to something that takes it (e.g.
 * g_paste_history_add()) or release it.
 *
 * @secret answers for the selection and not for @item, which is why it is told
 * apart from "@item is a password": an update that produced nothing still read
 * the selection, and a text deduped against the one already there is the only
 * word anything gets on whether what is sitting on it is still a secret.
 * %CLIPBOARD_SECRET_NO for a selection with no text (see
 * #GPasteClipboardMimeResults).
 */
typedef void (*GPasteClipboardProviderUpdateCallback) (GPasteClipboardProvider *self,
                                                       GPasteItem              *item,
                                                       gboolean                 superseded,
                                                       GPasteClipboardSecret    secret,
                                                       gpointer                 user_data);

/**
 * GPasteClipboardSyncCallback:
 * @other: the #GPasteClipboardProvider the text is going to
 * @text: the text the source selection was read as
 * @user_data: the data passed to g_paste_clipboard_provider_sync_text()
 *
 * Receives the text a sync read came back with, to put on @other.
 *
 * Publishing is the caller's rather than the backend's, because what a selection
 * carries afterwards is something the caller records: a backend writing to
 * @other itself would leave every record made for it saying what it carried
 * before. Not called at all when the read failed or the sync's own deadline ran
 * out first -- nothing was published, so nothing about @other has changed.
 */
typedef void (*GPasteClipboardSyncCallback) (GPasteClipboardProvider *other,
                                             const gchar             *text,
                                             gpointer                 user_data);

/**
 * GPasteClipboardProviderInterface:
 * @parent_iface: the parent interface
 *
 * The backend-agnostic clipboard surface the daemon drives. A provider owns a
 * single selection (the system clipboard or the primary selection), caches its
 * current content and emits #GPasteClipboardProvider::changed whenever the
 * selection ownership changes externally.
 *
 * None of the methods expose toolkit types: a provider may sit on top of GDK
 * (running against an X11/XWayland display) or on top of mutter's MetaSelection
 * (running inside gnome-shell), but the clipboards manager talks to all of them
 * through this single contract.
 *
 * Every vfunc is required, including is_reading: an asynchronous provider must
 * hide its committed cache until classification finishes. A default FALSE
 * would mistake stale password text for the current owner.
 */
struct _GPasteClipboardProviderInterface
{
    GTypeInterface parent_iface;

    gboolean     (*is_clipboard)       (GPasteClipboardProvider *self);
    const gchar *(*get_text)           (GPasteClipboardProvider *self);
    const gchar *(*get_image_checksum) (GPasteClipboardProvider *self);
    gboolean     (*is_reading)         (GPasteClipboardProvider *self);
    gboolean     (*is_empty)           (GPasteClipboardProvider *self);
    void         (*update)             (GPasteClipboardProvider              *self,
                                        GPasteClipboardProviderUpdateCallback callback,
                                        gpointer                              user_data);
    void         (*select_text)        (GPasteClipboardProvider *self,
                                        const gchar             *text);
    void         (*sync_text)          (GPasteClipboardProvider    *self,
                                        GPasteClipboardProvider    *other,
                                        GPasteClipboardSyncCallback callback,
                                        gpointer                    user_data,
                                        GDestroyNotify              destroy);
    gboolean     (*select_item)        (GPasteClipboardProvider *self,
                                        GPasteItem              *item);
    void         (*store)              (GPasteClipboardProvider *self);
};

/*
 * Define a backend's GPasteClipboardProviderInterface vtable thunks and its
 * <lc>_provider_iface_init. The backend must provide methods named
 * g_paste_clipboard_<lc>_<vfunc> (one per vfunc below, including is_empty) and
 * the instance cast macro G_PASTE_CLIPBOARD_<UC>. Expand once, after those
 * methods are defined.
 */
#define G_PASTE_CLIPBOARD_PROVIDER_DEFINE_VFUNCS(lc, UC)                                                   \
    static gboolean                                                                                        \
    provider_is_clipboard (GPasteClipboardProvider *self)                                                  \
    {                                                                                                      \
        return g_paste_clipboard_##lc##_is_clipboard (G_PASTE_CLIPBOARD_##UC ((gpointer) self));           \
    }                                                                                                      \
    static const gchar *                                                                                   \
    provider_get_text (GPasteClipboardProvider *self)                                                      \
    {                                                                                                      \
        return g_paste_clipboard_##lc##_get_text (G_PASTE_CLIPBOARD_##UC ((gpointer) self));               \
    }                                                                                                      \
    static const gchar *                                                                                   \
    provider_get_image_checksum (GPasteClipboardProvider *self)                                            \
    {                                                                                                      \
        return g_paste_clipboard_##lc##_get_image_checksum (G_PASTE_CLIPBOARD_##UC ((gpointer) self));     \
    }                                                                                                      \
    static gboolean                                                                                        \
    provider_is_reading (GPasteClipboardProvider *self)                                                    \
    {                                                                                                      \
        return g_paste_clipboard_##lc##_is_reading (G_PASTE_CLIPBOARD_##UC ((gpointer) self));             \
    }                                                                                                      \
    static gboolean                                                                                        \
    provider_is_empty (GPasteClipboardProvider *self)                                                      \
    {                                                                                                      \
        return g_paste_clipboard_##lc##_is_empty (G_PASTE_CLIPBOARD_##UC ((gpointer) self));               \
    }                                                                                                      \
    static void                                                                                            \
    provider_update (GPasteClipboardProvider              *self,                                           \
                     GPasteClipboardProviderUpdateCallback callback,                                       \
                     gpointer                              user_data)                                      \
    {                                                                                                      \
        g_paste_clipboard_##lc##_update (G_PASTE_CLIPBOARD_##UC ((gpointer) self), callback, user_data);   \
    }                                                                                                      \
    static void                                                                                            \
    provider_select_text (GPasteClipboardProvider *self,                                                   \
                          const gchar             *text)                                                   \
    {                                                                                                      \
        g_paste_clipboard_##lc##_select_text (G_PASTE_CLIPBOARD_##UC ((gpointer) self), text);             \
    }                                                                                                      \
    static void                                                                                            \
    provider_sync_text (GPasteClipboardProvider    *self,                                                  \
                        GPasteClipboardProvider    *other,                                                 \
                        GPasteClipboardSyncCallback callback,                                              \
                        gpointer                    user_data,                                             \
                        GDestroyNotify              destroy)                                               \
    {                                                                                                      \
        g_paste_clipboard_##lc##_sync_text (G_PASTE_CLIPBOARD_##UC ((gpointer) self),                      \
                                            G_PASTE_CLIPBOARD_##UC ((gpointer) other),                     \
                                            callback, user_data, destroy);                                 \
    }                                                                                                      \
    static gboolean                                                                                        \
    provider_select_item (GPasteClipboardProvider *self,                                                   \
                          GPasteItem              *item)                                                   \
    {                                                                                                      \
        return g_paste_clipboard_##lc##_select_item (G_PASTE_CLIPBOARD_##UC ((gpointer) self), item);      \
    }                                                                                                      \
    static void                                                                                            \
    provider_store (GPasteClipboardProvider *self)                                                         \
    {                                                                                                      \
        g_paste_clipboard_##lc##_store (G_PASTE_CLIPBOARD_##UC ((gpointer) self));                         \
    }                                                                                                      \
    static void                                                                                            \
    g_paste_clipboard_##lc##_provider_iface_init (GPasteClipboardProviderInterface *iface)                 \
    {                                                                                                      \
        iface->is_clipboard = provider_is_clipboard;                                                       \
        iface->get_text = provider_get_text;                                                               \
        iface->get_image_checksum = provider_get_image_checksum;                                           \
        iface->is_reading = provider_is_reading;                                                           \
        iface->is_empty = provider_is_empty;                                                               \
        iface->update = provider_update;                                                                   \
        iface->select_text = provider_select_text;                                                         \
        iface->sync_text = provider_sync_text;                                                             \
        iface->select_item = provider_select_item;                                                         \
        iface->store = provider_store;                                                                     \
    }

gboolean      g_paste_clipboard_provider_is_clipboard       (GPasteClipboardProvider *self);
const gchar  *g_paste_clipboard_provider_get_text           (GPasteClipboardProvider *self);
const gchar  *g_paste_clipboard_provider_get_image_checksum (GPasteClipboardProvider *self);
gboolean      g_paste_clipboard_provider_is_reading         (GPasteClipboardProvider *self);
gboolean      g_paste_clipboard_provider_is_empty           (GPasteClipboardProvider *self);
void          g_paste_clipboard_provider_update             (GPasteClipboardProvider              *self,
                                                             GPasteClipboardProviderUpdateCallback callback,
                                                             gpointer                              user_data);
void          g_paste_clipboard_provider_select_text        (GPasteClipboardProvider *self,
                                                             const gchar             *text);
void          g_paste_clipboard_provider_sync_text          (GPasteClipboardProvider    *self,
                                                             GPasteClipboardProvider    *other,
                                                             GPasteClipboardSyncCallback callback,
                                                             gpointer                    user_data,
                                                             GDestroyNotify              destroy);
gboolean      g_paste_clipboard_provider_select_item        (GPasteClipboardProvider *self,
                                                             GPasteItem              *item);
GPasteItem   *g_paste_clipboard_provider_ensure_not_empty   (GPasteClipboardProvider *self,
                                                             GPasteHistory           *history);
void          g_paste_clipboard_provider_store              (GPasteClipboardProvider *self);

void          g_paste_clipboard_provider_emit_changed       (GPasteClipboardProvider *self);

const gchar  *g_paste_clipboard_provider_target_name        (gboolean is_clipboard);

G_END_DECLS
