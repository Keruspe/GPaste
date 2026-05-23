// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-history.h>
#include <gpaste-keybinding.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_POP_KEYBINDING (g_paste_pop_keybinding_get_type ())

G_PASTE_FINAL_TYPE (PopKeybinding, pop_keybinding, POP_KEYBINDING, GPasteKeybinding)

GPasteKeybinding *g_paste_pop_keybinding_new (GPasteHistory *history);

G_END_DECLS
