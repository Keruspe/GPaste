/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#pragma once

#include <gpaste-history.h>
#include <gpaste-keybinding.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_MAKE_PASSWORD_KEYBINDING (g_paste_make_password_keybinding_get_type ())

G_PASTE_FINAL_TYPE (MakePasswordKeybinding, make_password_keybinding, MAKE_PASSWORD_KEYBINDING, GPasteKeybinding)

GPasteKeybinding *g_paste_make_password_keybinding_new (GPasteHistory *history);

G_END_DECLS
