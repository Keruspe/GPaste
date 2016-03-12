/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#ifndef __G_PASTE_APPLET_MENU_H__
#define __G_PASTE_APPLET_MENU_H__

#include <gpaste-client.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_APPLET_MENU (g_paste_applet_menu_get_type ())

G_PASTE_FINAL_TYPE (AppletMenu, applet_menu, APPLET_MENU, GtkMenu)

GtkWidget *g_paste_applet_menu_new (GPasteClient *client,
                                    GApplication *app);

G_END_DECLS

#endif /*__G_PASTE_APPLET_MENU_H__*/
