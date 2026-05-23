// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon.h>
#include <gpaste-keybinding.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UPLOAD_KEYBINDING (g_paste_upload_keybinding_get_type ())

G_PASTE_FINAL_TYPE (UploadKeybinding, upload_keybinding, UPLOAD_KEYBINDING, GPasteKeybinding)

GPasteKeybinding *g_paste_upload_keybinding_new (GPasteDaemon *daemon);

G_END_DECLS
