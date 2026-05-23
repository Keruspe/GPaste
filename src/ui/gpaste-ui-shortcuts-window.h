// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_SHORTCUTS_WINDOW_H__
#define __G_PASTE_UI_SHORTCUTS_WINDOW_H__

#include <gpaste/gpaste-settings.h>

#include <adwaita.h>

G_BEGIN_DECLS

GtkWidget *g_paste_ui_shortcuts_window_new (const GPasteSettings *settings);

G_END_DECLS

#endif /*__G_PASTE_UI_SHORTCUTS_WINDOW_H__*/
