// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-clipboard-provider.h>

#include <meta/meta-selection.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_CLIPBOARD_META (g_paste_clipboard_meta_get_type ())

G_PASTE_FINAL_TYPE (ClipboardMeta, clipboard_meta, CLIPBOARD_META, GObject)

GPasteClipboardProvider *g_paste_clipboard_meta_new_clipboard (MetaSelection  *selection,
                                                              GPasteSettings *settings);
GPasteClipboardProvider *g_paste_clipboard_meta_new_primary   (MetaSelection  *selection,
                                                              GPasteSettings *settings);

G_END_DECLS
