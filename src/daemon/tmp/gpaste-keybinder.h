/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_KEYBINDER_H__
#define __G_PASTE_KEYBINDER_H__

#include <gpaste/gpaste-gnome-shell-client.h>

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

#endif /*__G_PASTE_KEYBINDER_H__*/
