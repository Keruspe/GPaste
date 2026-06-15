// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_KEYBINDING (g_paste_keybinding_get_type ())

G_PASTE_FINAL_TYPE (Keybinding, keybinding, KEYBINDING, GObject)

typedef const gchar *(*GPasteKeybindingGetter) (const GPasteSettings *settings);
typedef void         (*GPasteKeybindingFunc)   (GPasteKeybinding *self,
                                                gpointer          data);

const gchar    *g_paste_keybinding_get_dconf_key   (const GPasteKeybinding *self);
const gchar    *g_paste_keybinding_get_description (const GPasteKeybinding *self);
const gchar    *g_paste_keybinding_get_accelerator (const GPasteKeybinding *self,
                                                    const GPasteSettings   *settings);

void            g_paste_keybinding_activate      (GPasteKeybinding *self,
                                                  GPasteSettings   *settings);
void            g_paste_keybinding_deactivate    (GPasteKeybinding *self);
gboolean        g_paste_keybinding_is_active     (GPasteKeybinding *self);
void            g_paste_keybinding_perform       (GPasteKeybinding *self);

GPasteKeybinding *g_paste_keybinding_new (const gchar           *dconf_key,
                                          const gchar           *description,
                                          GPasteKeybindingGetter getter,
                                          GPasteKeybindingFunc   callback,
                                          gpointer               user_data);

G_END_DECLS
