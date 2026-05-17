/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#pragma once

#include <gpaste/gpaste-macros.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_INTERNAL_KEYBINDING_PROVIDER (g_paste_internal_keybinding_provider_get_type ())

G_PASTE_FINAL_TYPE (InternalKeybindingProvider, internal_keybinding_provider, INTERNAL_KEYBINDING_PROVIDER, GObject)

GPasteInternalKeybindingProvider *g_paste_internal_keybinding_provider_new (void);

void g_paste_internal_keybinding_provider_change_grab (GPasteInternalKeybindingProvider *self,
                                                       const guint32                    *keycodes,
                                                       GdkModifierType                   modifiers,
                                                       gboolean                          grab);

G_END_DECLS
