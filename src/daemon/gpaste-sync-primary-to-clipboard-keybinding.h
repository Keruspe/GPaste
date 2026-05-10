/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#pragma once

#include <gpaste-clipboards-manager.h>
#include <gpaste-keybinding.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_SYNC_PRIMARY_TO_CLIPBOARD_KEYBINDING (g_paste_sync_primary_to_clipboard_keybinding_get_type ())

G_PASTE_FINAL_TYPE (SyncPrimaryToClipboardKeybinding, sync_primary_to_clipboard_keybinding, SYNC_PRIMARY_TO_CLIPBOARD_KEYBINDING, GPasteKeybinding)

GPasteKeybinding *g_paste_sync_primary_to_clipboard_keybinding_new (GPasteClipboardsManager *gcm);

G_END_DECLS
