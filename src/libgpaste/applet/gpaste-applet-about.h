/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#ifndef __G_PASTE_APPLET_ABOUT_H__
#define __G_PASTE_APPLET_ABOUT_H__

#include <gpaste-client.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_APPLET_ABOUT (g_paste_applet_about_get_type ())

G_PASTE_FINAL_TYPE (AppletAbout, applet_about, APPLET_ABOUT, GtkMenuItem)

GtkWidget *g_paste_applet_about_new (GPasteClient *client);

G_END_DECLS

#endif /*__G_PASTE_APPLET_ABOUT_H__*/
