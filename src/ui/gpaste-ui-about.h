/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_ABOUT_H__
#define __G_PASTE_UI_ABOUT_H__

#include <gpaste/gpaste-macros.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_ABOUT (g_paste_ui_about_get_type ())

G_PASTE_FINAL_TYPE (UiAbout, ui_about, UI_ABOUT, GtkButton)

GtkWidget *g_paste_ui_about_new (GtkApplication *app);

G_END_DECLS

#endif /*__G_PASTE_UI_ABOUT_H__*/
