/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#ifndef __G_PASTE_APPLET_APP_INDICATOR_H__
#define __G_PASTE_APPLET_APP_INDICATOR_H__

#include <gpaste-applet-icon.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_APPLET_APP_INDICATOR (g_paste_applet_app_indicator_get_type ())

G_PASTE_FINAL_TYPE (AppletAppIndicator, applet_app_indicator, APPLET_APP_INDICATOR, GPasteAppletIcon)

GPasteAppletIcon *g_paste_applet_app_indicator_new (GPasteClient *client,
                                                    GApplication *app);

G_END_DECLS

#endif /*__G_PASTE_APPLET_APP_INDICATOR_H__*/
