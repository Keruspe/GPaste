/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#ifndef __G_PASTE_APPLET_UI_H__
#define __G_PASTE_APPLET_UI_H__

#include <gpaste-macros.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_APPLET_UI (g_paste_applet_ui_get_type ())

G_PASTE_FINAL_TYPE (AppletUi, applet_ui, APPLET_UI, GtkMenuItem)

GtkWidget *g_paste_applet_ui_new (void);

G_END_DECLS

#endif /*__G_PASTE_APPLET_UI_H__*/
