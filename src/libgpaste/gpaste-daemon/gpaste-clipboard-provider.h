// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-history.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_CLIPBOARD_PROVIDER (g_paste_clipboard_provider_get_type ())

G_PASTE_VISIBLE
G_DECLARE_INTERFACE (GPasteClipboardProvider, g_paste_clipboard_provider, G_PASTE, CLIPBOARD_PROVIDER, GObject)

G_PASTE_CONST_FUNCS (ClipboardProvider, CLIPBOARD_PROVIDER)

typedef void (*GPasteClipboardProviderUpdateCallback) (GPasteClipboardProvider *self,
                                                       GPasteItem              *item,
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
 */
struct _GPasteClipboardProviderInterface
{
    GTypeInterface parent_iface;

    gboolean     (*is_clipboard)       (const GPasteClipboardProvider *self);
    const gchar *(*get_text)           (const GPasteClipboardProvider *self);
    const gchar *(*get_image_checksum) (const GPasteClipboardProvider *self);
    gboolean     (*is_empty)           (const GPasteClipboardProvider *self);
    void         (*update)             (GPasteClipboardProvider              *self,
                                        GPasteClipboardProviderUpdateCallback callback,
                                        gpointer                              user_data);
    void         (*select_text)        (GPasteClipboardProvider *self,
                                        const gchar             *text);
    void         (*sync_text)          (const GPasteClipboardProvider *self,
                                        GPasteClipboardProvider       *other);
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
#define G_PASTE_CLIPBOARD_PROVIDER_DEFINE_VFUNCS(lc, UC)                                                  \
    static gboolean                                                                                       \
    provider_is_clipboard (const GPasteClipboardProvider *self)                                           \
    {                                                                                                      \
        return g_paste_clipboard_##lc##_is_clipboard (G_PASTE_CLIPBOARD_##UC ((gpointer) self));          \
    }                                                                                                      \
    static const gchar *                                                                                  \
    provider_get_text (const GPasteClipboardProvider *self)                                               \
    {                                                                                                      \
        return g_paste_clipboard_##lc##_get_text (G_PASTE_CLIPBOARD_##UC ((gpointer) self));              \
    }                                                                                                      \
    static const gchar *                                                                                  \
    provider_get_image_checksum (const GPasteClipboardProvider *self)                                     \
    {                                                                                                      \
        return g_paste_clipboard_##lc##_get_image_checksum (G_PASTE_CLIPBOARD_##UC ((gpointer) self));    \
    }                                                                                                      \
    static gboolean                                                                                       \
    provider_is_empty (const GPasteClipboardProvider *self)                                               \
    {                                                                                                      \
        return g_paste_clipboard_##lc##_is_empty (G_PASTE_CLIPBOARD_##UC ((gpointer) self));              \
    }                                                                                                      \
    static void                                                                                           \
    provider_update (GPasteClipboardProvider              *self,                                          \
                     GPasteClipboardProviderUpdateCallback callback,                                      \
                     gpointer                              user_data)                                     \
    {                                                                                                      \
        g_paste_clipboard_##lc##_update (G_PASTE_CLIPBOARD_##UC ((gpointer) self), callback, user_data);  \
    }                                                                                                      \
    static void                                                                                           \
    provider_select_text (GPasteClipboardProvider *self,                                                  \
                          const gchar             *text)                                                  \
    {                                                                                                      \
        g_paste_clipboard_##lc##_select_text (G_PASTE_CLIPBOARD_##UC ((gpointer) self), text);            \
    }                                                                                                      \
    static void                                                                                           \
    provider_sync_text (const GPasteClipboardProvider *self,                                              \
                        GPasteClipboardProvider       *other)                                             \
    {                                                                                                      \
        g_paste_clipboard_##lc##_sync_text (G_PASTE_CLIPBOARD_##UC ((gpointer) self),                     \
                                            G_PASTE_CLIPBOARD_##UC ((gpointer) other));                   \
    }                                                                                                      \
    static gboolean                                                                                       \
    provider_select_item (GPasteClipboardProvider *self,                                                  \
                          GPasteItem              *item)                                                  \
    {                                                                                                      \
        return g_paste_clipboard_##lc##_select_item (G_PASTE_CLIPBOARD_##UC ((gpointer) self), item);     \
    }                                                                                                      \
    static void                                                                                           \
    provider_store (GPasteClipboardProvider *self)                                                        \
    {                                                                                                      \
        g_paste_clipboard_##lc##_store (G_PASTE_CLIPBOARD_##UC ((gpointer) self));                        \
    }                                                                                                      \
    static void                                                                                           \
    g_paste_clipboard_##lc##_provider_iface_init (GPasteClipboardProviderInterface *iface)                \
    {                                                                                                      \
        iface->is_clipboard = provider_is_clipboard;                                                      \
        iface->get_text = provider_get_text;                                                              \
        iface->get_image_checksum = provider_get_image_checksum;                                          \
        iface->is_empty = provider_is_empty;                                                              \
        iface->update = provider_update;                                                                  \
        iface->select_text = provider_select_text;                                                        \
        iface->sync_text = provider_sync_text;                                                            \
        iface->select_item = provider_select_item;                                                        \
        iface->store = provider_store;                                                                    \
    }

gboolean      g_paste_clipboard_provider_is_clipboard       (const GPasteClipboardProvider *self);
const gchar  *g_paste_clipboard_provider_get_text           (const GPasteClipboardProvider *self);
const gchar  *g_paste_clipboard_provider_get_image_checksum (const GPasteClipboardProvider *self);
gboolean      g_paste_clipboard_provider_is_empty           (const GPasteClipboardProvider *self);
void          g_paste_clipboard_provider_update             (GPasteClipboardProvider              *self,
                                                             GPasteClipboardProviderUpdateCallback callback,
                                                             gpointer                              user_data);
void          g_paste_clipboard_provider_select_text        (GPasteClipboardProvider *self,
                                                             const gchar             *text);
void          g_paste_clipboard_provider_sync_text          (const GPasteClipboardProvider *self,
                                                             GPasteClipboardProvider       *other);
gboolean      g_paste_clipboard_provider_select_item        (GPasteClipboardProvider *self,
                                                             GPasteItem              *item);
void          g_paste_clipboard_provider_ensure_not_empty   (GPasteClipboardProvider *self,
                                                             GPasteHistory           *history);
void          g_paste_clipboard_provider_store              (GPasteClipboardProvider *self);

void          g_paste_clipboard_provider_emit_changed       (GPasteClipboardProvider *self);

const gchar  *g_paste_clipboard_provider_target_name        (gboolean is_clipboard);

G_END_DECLS
