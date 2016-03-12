/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_APPLET_STATUS_ICON_H__
#define __G_PASTE_APPLET_STATUS_ICON_H__

#include <gpaste-applet-icon.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_APPLET_STATUS_ICON (g_paste_applet_status_icon_get_type ())

G_PASTE_FINAL_TYPE (AppletStatusIcon, applet_status_icon, APPLET_STATUS_ICON, GPasteAppletIcon)

GPasteAppletIcon *g_paste_applet_status_icon_new (GPasteClient *client,
                                                  GApplication *app);

G_END_DECLS

#endif /*__G_PASTE_APPLET_STATUS_ICON_H__*/
