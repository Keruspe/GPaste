/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#pragma once

#include <gpaste/gpaste-keybinding-provider.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_INTERNAL_KEYBINDING_PROVIDER (g_paste_internal_keybinding_provider_get_type ())

G_PASTE_FINAL_TYPE (InternalKeybindingProvider, internal_keybinding_provider, INTERNAL_KEYBINDING_PROVIDER, GObject)

GPasteInternalKeybindingProvider *g_paste_internal_keybinding_provider_new (void);

G_END_DECLS
