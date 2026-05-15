/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#pragma once

#include <gpaste-clipboards-manager.h>
#include <gpaste-keybinding.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_SYNC_CLIPBOARD_TO_PRIMARY_KEYBINDING (g_paste_sync_clipboard_to_primary_keybinding_get_type ())

G_PASTE_FINAL_TYPE (SyncClipboardToPrimaryKeybinding, sync_clipboard_to_primary_keybinding, SYNC_CLIPBOARD_TO_PRIMARY_KEYBINDING, GPasteKeybinding)

GPasteKeybinding *g_paste_sync_clipboard_to_primary_keybinding_new (GPasteClipboardsManager *gcm);

G_END_DECLS
