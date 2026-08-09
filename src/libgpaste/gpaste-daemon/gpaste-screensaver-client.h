// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-macros.h>

G_BEGIN_DECLS

#define G_PASTE_SCREENSAVER_BUS_NAME "org.gnome.ScreenSaver"

#define G_PASTE_TYPE_SCREENSAVER_CLIENT (g_paste_screensaver_client_get_type ())

G_PASTE_FINAL_TYPE (ScreensaverClient, screensaver_client, SCREENSAVER_CLIENT, GDBusProxy)

gboolean                 g_paste_screensaver_client_is_active  (GPasteScreensaverClient *self);

GPasteScreensaverClient *g_paste_screensaver_client_new_sync   (GError **error);
void                     g_paste_screensaver_client_new        (GAsyncReadyCallback callback,
                                                                gpointer            user_data);
GPasteScreensaverClient *g_paste_screensaver_client_new_finish (GAsyncResult       *result,
                                                                GError            **error);

G_END_DECLS
