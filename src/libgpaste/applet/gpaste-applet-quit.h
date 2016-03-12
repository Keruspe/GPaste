/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#ifndef __G_PASTE_APPLET_QUIT_H__
#define __G_PASTE_APPLET_QUIT_H__

#include <gpaste-macros.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_APPLET_QUIT (g_paste_applet_quit_get_type ())

G_PASTE_FINAL_TYPE (AppletQuit, applet_quit, APPLET_QUIT, GtkMenuItem)

GtkWidget *g_paste_applet_quit_new (GApplication *app);

G_END_DECLS

#endif /*__G_PASTE_APPLET_QUIT_H__*/
