/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_SHORTCUTS_WINDOW_H__
#define __G_PASTE_UI_SHORTCUTS_WINDOW_H__

#include <gpaste-settings.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_SHORTCUTS_WINDOW (g_paste_ui_shortcuts_window_get_type ())

G_PASTE_FINAL_TYPE (UiShortcutsWindow, ui_shortcuts_window, UI_SHORTCUTS_WINDOW, GtkShortcutsWindow)

GtkWidget *g_paste_ui_shortcuts_window_new (const GPasteSettings *settings);

G_END_DECLS

#endif /*__G_PASTE_UI_SHORTCUTS_WINDOW_H__*/
