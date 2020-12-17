/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_SETTINGS_H__
#define __G_PASTE_UI_SETTINGS_H__

#include <gpaste-macros.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_SETTINGS (g_paste_ui_settings_get_type ())

G_PASTE_FINAL_TYPE (UiSettings, ui_settings, UI_SETTINGS, GtkMenuButton)

GtkWidget *g_paste_ui_settings_new (void);

G_END_DECLS

#endif /*__G_PASTE_UI_SETTINGS_H__*/
