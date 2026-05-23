// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon.h>
#include <gpaste-keybinding.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_SHOW_HISTORY_KEYBINDING (g_paste_show_history_keybinding_get_type ())

G_PASTE_FINAL_TYPE (ShowHistoryKeybinding, show_history_keybinding, SHOW_HISTORY_KEYBINDING, GPasteKeybinding)

GPasteKeybinding *g_paste_show_history_keybinding_new (GPasteDaemon *gpaste_daemon);

G_END_DECLS
