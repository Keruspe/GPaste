// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-clipboard-provider.h>

G_BEGIN_DECLS

/* Independent publications start a new copy. Maintenance completes or clears
 * the current one; a nested explicit selection still starts a copy of its own. */
gboolean g_paste_clipboard_provider_select_item_full (GPasteClipboardProvider *self,
                                                      GPasteItem              *item,
                                                      gboolean                 independent);
void     g_paste_clipboard_provider_select_text_full (GPasteClipboardProvider *self,
                                                      const gchar             *text,
                                                      gboolean                 independent);

G_END_DECLS
