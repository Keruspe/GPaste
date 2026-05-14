/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#pragma once

#include <gpaste-keybinding.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_KEYBINDER (g_paste_keybinder_get_type ())

G_PASTE_FINAL_TYPE (Keybinder, keybinder, KEYBINDER, GObject)

void g_paste_keybinder_add_keybinding (GPasteKeybinder  *self,
                                       GPasteKeybinding *binding);
void g_paste_keybinder_activate_all   (GPasteKeybinder  *self);
void g_paste_keybinder_deactivate_all (GPasteKeybinder  *self);

GPasteKeybinder *g_paste_keybinder_new (GPasteSettings         *settings,
                                        GPasteGnomeShellClient *shell_client);

G_END_DECLS
