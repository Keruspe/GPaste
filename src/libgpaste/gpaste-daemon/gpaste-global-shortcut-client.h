// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste/gpaste-macros.h>

G_BEGIN_DECLS

#define G_PASTE_GLOBAL_SHORTCUT_BUS_NAME "org.freedesktop.portal.Desktop"

/**
 * GPasteKeybindingAccelerator:
 * @id: a unique string identifier for this shortcut (the dconf key)
 * @accelerator: the trigger string (e.g., "<Super>V")
 * @description: a human-readable, translated description shown in the portal UI
 *
 * Represents a global shortcut to be registered with a #GPasteGlobalShortcutClient.
 * Terminate an array of these with an entry whose @id is %NULL.
 */
typedef struct
{
    const gchar *id;
    const gchar *accelerator;
    const gchar *description;
} GPasteKeybindingAccelerator;

#define G_PASTE_KEYBINDING_ACCELERATOR(id, accelerator, description) \
    ((GPasteKeybindingAccelerator) { (id), (accelerator), (description) })

#define G_PASTE_TYPE_GLOBAL_SHORTCUT_CLIENT (g_paste_global_shortcut_client_get_type ())

G_PASTE_FINAL_TYPE (GlobalShortcutClient, global_shortcut_client, GLOBAL_SHORTCUT_CLIENT, GDBusProxy)

/***********/
/* Methods */
/***********/

void g_paste_global_shortcut_client_grab_all   (GPasteGlobalShortcutClient     *self,
                                                    const GPasteKeybindingAccelerator *accels);
void g_paste_global_shortcut_client_ungrab_all (GPasteGlobalShortcutClient     *self);

/****************/
/* Constructors */
/****************/

GPasteGlobalShortcutClient *g_paste_global_shortcut_client_new_sync   (GError             **error);
void                           g_paste_global_shortcut_client_new        (GAsyncReadyCallback  callback,
                                                                              gpointer             user_data);
GPasteGlobalShortcutClient *g_paste_global_shortcut_client_new_finish (GAsyncResult        *result,
                                                                              GError             **error);

G_END_DECLS
