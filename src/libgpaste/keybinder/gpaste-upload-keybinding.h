/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UPLOAD_KEYBINDING_H__
#define __G_PASTE_UPLOAD_KEYBINDING_H__

#include <gpaste-daemon.h>
#include <gpaste-keybinding.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UPLOAD_KEYBINDING (g_paste_upload_keybinding_get_type ())

G_PASTE_FINAL_TYPE (UploadKeybinding, upload_keybinding, UPLOAD_KEYBINDING, GPasteKeybinding)

GPasteKeybinding *g_paste_upload_keybinding_new (GPasteDaemon *daemon);

G_END_DECLS

#endif /*__G_PASTE_UPLOAD_KEYBINDING_H__*/
