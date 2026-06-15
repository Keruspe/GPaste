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
    void         (*update)             (GPasteClipboardProvider              *self,
                                        GPasteClipboardProviderUpdateCallback callback,
                                        gpointer                              user_data);
    void         (*select_text)        (GPasteClipboardProvider *self,
                                        const gchar             *text);
    void         (*sync_text)          (const GPasteClipboardProvider *self,
                                        GPasteClipboardProvider       *other);
    gboolean     (*select_item)        (GPasteClipboardProvider *self,
                                        GPasteItem              *item);
    void         (*ensure_not_empty)   (GPasteClipboardProvider *self,
                                        GPasteHistory           *history);
    void         (*store)              (GPasteClipboardProvider *self);
};

gboolean      g_paste_clipboard_provider_is_clipboard       (const GPasteClipboardProvider *self);
const gchar  *g_paste_clipboard_provider_get_text           (const GPasteClipboardProvider *self);
const gchar  *g_paste_clipboard_provider_get_image_checksum (const GPasteClipboardProvider *self);
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

G_END_DECLS
