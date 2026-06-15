// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-clipboard-provider.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_CLIPBOARD_GDK (g_paste_clipboard_gdk_get_type ())

G_PASTE_FINAL_TYPE (ClipboardGdk, clipboard_gdk, CLIPBOARD_GDK, GObject)

GPasteClipboardProvider *g_paste_clipboard_gdk_new_clipboard (GPasteSettings *settings);
GPasteClipboardProvider *g_paste_clipboard_gdk_new_primary   (GPasteSettings *settings);

G_END_DECLS
