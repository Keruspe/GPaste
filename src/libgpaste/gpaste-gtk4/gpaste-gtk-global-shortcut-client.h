/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_GTK4_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste-gtk4.h> can be included directly."
#endif

#pragma once

#include <gpaste/gpaste-macros.h>

G_BEGIN_DECLS

#define G_PASTE_GTK_GLOBAL_SHORTCUT_BUS_NAME "org.freedesktop.portal.Desktop"

#define G_PASTE_TYPE_GTK_GLOBAL_SHORTCUT_CLIENT (g_paste_gtk_global_shortcut_client_get_type ())

G_PASTE_FINAL_TYPE (GtkGlobalShortcutClient, gtk_global_shortcut_client, GTK_GLOBAL_SHORTCUT_CLIENT, GDBusProxy)

/****************/
/* Constructors */
/****************/

GPasteGtkGlobalShortcutClient *g_paste_gtk_global_shortcut_client_new_sync   (GError             **error);
void                           g_paste_gtk_global_shortcut_client_new        (GAsyncReadyCallback  callback,
                                                                              gpointer             user_data);
GPasteGtkGlobalShortcutClient *g_paste_gtk_global_shortcut_client_new_finish (GAsyncResult        *result,
                                                                              GError             **error);

G_END_DECLS
