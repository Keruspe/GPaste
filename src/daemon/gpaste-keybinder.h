// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-keybinding.h>
#include <gpaste/gpaste-keybinding-provider.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_KEYBINDER (g_paste_keybinder_get_type ())

G_PASTE_FINAL_TYPE (Keybinder, keybinder, KEYBINDER, GObject)

void g_paste_keybinder_add_keybinding (GPasteKeybinder          *self,
                                       GPasteKeybinding         *binding);
void g_paste_keybinder_activate_all   (GPasteKeybinder          *self);
void g_paste_keybinder_deactivate_all (GPasteKeybinder          *self);

GPasteKeybinder *g_paste_keybinder_new (GPasteSettings           *settings,
                                        GPasteKeybindingProvider *provider);

G_END_DECLS
