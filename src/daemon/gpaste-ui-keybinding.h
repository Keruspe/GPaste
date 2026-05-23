// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-keybinding.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_KEYBINDING (g_paste_ui_keybinding_get_type ())

G_PASTE_FINAL_TYPE (UiKeybinding, ui_keybinding, UI_KEYBINDING, GPasteKeybinding)

GPasteKeybinding *g_paste_ui_keybinding_new (void);

G_END_DECLS
