// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_GTK4_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste-gtk4.h> can be included directly."
#endif

#pragma once

#include <gpaste/gpaste-macros.h>

G_BEGIN_DECLS

#define G_PASTE_GTK_GLOBAL_SHORTCUT_BUS_NAME "org.freedesktop.portal.Desktop"

/**
 * GPasteKeybindingAccelerator:
 * @id: a unique string identifier for this shortcut (the dconf key)
 * @accelerator: the trigger string (e.g., "<Super>V")
 * @description: a human-readable, translated description shown in the portal UI
 *
 * Represents a global shortcut to be registered with a #GPasteGtkGlobalShortcutClient.
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

#define G_PASTE_TYPE_GTK_GLOBAL_SHORTCUT_CLIENT (g_paste_gtk_global_shortcut_client_get_type ())

G_PASTE_FINAL_TYPE (GtkGlobalShortcutClient, gtk_global_shortcut_client, GTK_GLOBAL_SHORTCUT_CLIENT, GDBusProxy)

/***********/
/* Methods */
/***********/

void g_paste_gtk_global_shortcut_client_grab_all   (GPasteGtkGlobalShortcutClient     *self,
                                                    const GPasteKeybindingAccelerator *accels);
void g_paste_gtk_global_shortcut_client_ungrab_all (GPasteGtkGlobalShortcutClient     *self);

/****************/
/* Constructors */
/****************/

GPasteGtkGlobalShortcutClient *g_paste_gtk_global_shortcut_client_new_sync   (GError             **error);
void                           g_paste_gtk_global_shortcut_client_new        (GAsyncReadyCallback  callback,
                                                                              gpointer             user_data);
GPasteGtkGlobalShortcutClient *g_paste_gtk_global_shortcut_client_new_finish (GAsyncResult        *result,
                                                                              GError             **error);

G_END_DECLS
