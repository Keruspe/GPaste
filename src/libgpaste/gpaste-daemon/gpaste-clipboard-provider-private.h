// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-clipboard-provider.h>

G_BEGIN_DECLS

/* Independent publications start a new copy. Maintenance completes or clears
 * the current one; a nested explicit selection still starts a copy of its own.
 * Only a publication that happened is announced (::published). */
GPasteClipboardPublication g_paste_clipboard_provider_select_item_full (GPasteClipboardProvider *self,
                                                                        GPasteItem              *item,
                                                                        gboolean                 independent);
GPasteClipboardPublication g_paste_clipboard_provider_select_text_full (GPasteClipboardProvider *self,
                                                                        const gchar             *text,
                                                                        gboolean                 independent);

/* g_paste_clipboard_provider_update () saying whether the caller records the
 * copy read: see @record on #GPasteClipboardUpdate. */
void                       g_paste_clipboard_provider_update_full      (GPasteClipboardProvider              *self,
                                                                        gboolean                              record,
                                                                        GPasteClipboardProviderUpdateCallback callback,
                                                                        gpointer                              user_data);

/* Here rather than beside emit_changed (): only the in-tree GDK backend emits
 * ::reidentify, and only the clipboards manager listens, so there is nothing for
 * a binding to call. */
void                       g_paste_clipboard_provider_emit_reidentify  (GPasteClipboardProvider *self);

G_END_DECLS
