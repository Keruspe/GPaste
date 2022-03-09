/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gpaste/gpaste-macros.h>

G_BEGIN_DECLS

#define G_PASTE_SCREENSAVER_BUS_NAME "org.gnome.ScreenSaver"

#define G_PASTE_TYPE_SCREENSAVER_CLIENT (g_paste_screensaver_client_get_type ())

G_PASTE_FINAL_TYPE (ScreensaverClient, screensaver_client, SCREENSAVER_CLIENT, GDBusProxy)

GPasteScreensaverClient *g_paste_screensaver_client_new_sync   (GError **error);
void                     g_paste_screensaver_client_new        (GAsyncReadyCallback callback,
                                                                gpointer            user_data);
GPasteScreensaverClient *g_paste_screensaver_client_new_finish (GAsyncResult       *result,
                                                                GError            **error);

G_END_DECLS
